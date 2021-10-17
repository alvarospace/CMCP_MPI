#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "mpi.h"

/*
 * Un paso del método de Jacobi para la ecuación de Poisson
 *
 *   Argumentos:
 *     - N,M: dimensiones de la malla
 *     - Entrada: x es el vector de la iteración anterior, b es la parte derecha del sistema
 *     - Salida: t es el nuevo vector
 *
 *   Se asume que x,b,t son de dimensión (N+2)*(M+2), se recorren solo los puntos interiores
 *   de la malla, y en los bordes están almacenadas las condiciones de frontera (por defecto 0).
 */


void jacobi_step(int N,int M,double *x,double *b,double *t, int rank, int size)
{
  int i, j, ld=M+2;
  int next, prev;

  // Definición de emisores y receptores para comunicación pares-impares
  if (!rank) prev = MPI_PROC_NULL;
  else prev = rank-1;
  if (rank == size-1) next = MPI_PROC_NULL;
  else next = rank+1;

  if (!rank){
    MPI_Send(&x[N*ld],ld,MPI_DOUBLE,next,0,MPI_COMM_WORLD);
    MPI_Recv(&x[(N+1)*ld],ld,MPI_DOUBLE,next,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
  }
  else if(rank == size-1){
    MPI_Recv(&x[0*ld],ld,MPI_DOUBLE,prev,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
    MPI_Send(&x[1*ld],ld,MPI_DOUBLE,prev,0,MPI_COMM_WORLD);
  }
  else if (rank%2 == 0){
    //Los pares envían primero al siguiente y al anterior, y luego, reciben del anterior y del siguiente
    MPI_Send(&x[N*ld],ld,MPI_DOUBLE,next,0,MPI_COMM_WORLD);
    MPI_Send(&x[1*ld],ld,MPI_DOUBLE,prev,0,MPI_COMM_WORLD);
    MPI_Recv(&x[0*ld],ld,MPI_DOUBLE,prev,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
    MPI_Recv(&x[(N+1)*ld],ld,MPI_DOUBLE,next,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
  }
  else{
    MPI_Recv(&x[0*ld],ld,MPI_DOUBLE,prev,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
    MPI_Recv(&x[(N+1)*ld],ld,MPI_DOUBLE,next,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
    MPI_Send(&x[N*ld],ld,MPI_DOUBLE,next,0,MPI_COMM_WORLD);
    MPI_Send(&x[1*ld],ld,MPI_DOUBLE,prev,0,MPI_COMM_WORLD);
  }
 
  for (i=1; i<=N; i++) {
    for (j=1; j<=M; j++) {
      t[i*ld+j] = (b[i*ld+j] + x[(i+1)*ld+j] + x[(i-1)*ld+j] + x[i*ld+(j+1)] + x[i*ld+(j-1)])/4.0;
    }
  }
}

/*
 * Método de Jacobi para la ecuación de Poisson
 *
 *   Suponemos definida una malla de (N+1)x(M+1) puntos, donde los puntos
 *   de la frontera tienen definida una condición de contorno.
 *
 *   Esta función resuelve el sistema Ax=b mediante el método iterativo
 *   estacionario de Jacobi. La matriz A no se almacena explícitamente y
 *   se aplica de forma implícita para cada punto de la malla. El vector
 *   x representa la solución de la ecuación de Poisson en cada uno de los
 *   puntos de la malla (incluyendo el contorno). El vector b es la parte
 *   derecha del sistema de ecuaciones, y contiene el término h^2*f.
 *
 *   Suponemos que las condiciones de contorno son igual a 0 en toda la
 *   frontera del dominio.
 */
void jacobi_poisson(int N,int M,double *x,double *b, int rank, int size)
{
  int i, j, k, ld=M+2, conv, maxit=100;
  double *t, local_s, total_s, tol=1e-6;

  t = (double*)calloc((N+2)*(M+2),sizeof(double));

  k = 0;
  conv = 0;

  while (!conv && k<maxit) {

    /* calcula siguiente vector */
    jacobi_step(N,M,x,b,t,rank,size);

    /* criterio de parada: ||x_{k}-x_{k+1}||<tol */
    local_s = 0.0;
    for (i=1; i<=N; i++) {
      for (j=1; j<=M; j++) {
        local_s += (x[i*ld+j]-t[i*ld+j])*(x[i*ld+j]-t[i*ld+j]);
      }
    }

    MPI_Allreduce(&local_s, &total_s, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

    conv = (sqrt(total_s)<tol);

    if (!rank){
      printf("Error en iteración %d: %g\n", k, sqrt(total_s));
    }

    /* siguiente iteración */
    k = k+1;
    for (i=1; i<=N; i++) {
      for (j=1; j<=M; j++) {
        x[i*ld+j] = t[i*ld+j];
      }
    }

  }

  free(t);
}

int main(int argc, char **argv)
{
  int i, j, N=40, M=50, ld;
  double *x, *b, *sol, h=0.01, f=1.5;
  int rank, size;


  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);


  /* Extracción de argumentos */
  if (argc > 1) { /* El usuario ha indicado el valor de N */
    if ((N = atoi(argv[1])) < 0) N = 40;
  }
  if (argc > 2) { /* El usuario ha indicado el valor de M */
    if ((M = atoi(argv[2])) < 0) M = 1;
  }
  ld = M+2;  /* leading dimension */

  int n = N / size;

  /* Reserva de memoria */
  x = (double*)calloc((n+2)*(M+2),sizeof(double));
  b = (double*)calloc((n+2)*(M+2),sizeof(double));

  /* Inicializar datos */
  for (i=1; i<=n; i++) {
    for (j=1; j<=M; j++) {
      b[i*ld+j] = h*h*f;  /* suponemos que la función f es constante en todo el dominio */
    }
  }

  /* Resolución del sistema por el método de Jacobi */
  jacobi_poisson(n,M,x,b,rank,size);

  /* Imprimir solución (solo para comprobación, eliminar en el caso de problemas grandes) */

  sol = (double*)calloc((N+2)*(M+2),sizeof(double));
  if (!rank){
    int next = rank + 1;
    for (i=1; i<size; i++){
      MPI_Recv(&sol[(next*n+1)*ld],n*ld,MPI_DOUBLE,next,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
      next++;
    }
    for (i=1; i<=n; i++) {
      for (j=1; j<=M; j++) {
        sol[i*ld+j] = x[i*ld+j];
      }
    }
  }
  else{
    MPI_Send(&x[ld],n*ld,MPI_DOUBLE,0,0,MPI_COMM_WORLD);
  }

  if (!rank){
    for (i=1; i<=N; i++) {
      for (j=1; j<=M; j++) {
        printf("%g ", sol[i*ld+j]);
      }
      printf("\n");
    }
  }
  

  free(x);
  free(b);
  free(sol);

  MPI_Finalize();
  return 0;
}


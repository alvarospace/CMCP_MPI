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
void jacobi_step(int N,int M,double *x,double *b,double *t, MPI_Comm *comm_cart)
{
  int ld = M+2;
  int rank;
  MPI_Comm_rank(*comm_cart, &rank);

  // Identificamos los vecinos de la malla

  enum DIRS {DOWN, UP, LEFT, RIGHT};
  int neighbours_ranks[4];

  MPI_Cart_shift( *comm_cart , 0 , 1 , &neighbours_ranks[LEFT] , &neighbours_ranks[RIGHT]);
  MPI_Cart_shift( *comm_cart , 1 , 1 , &neighbours_ranks[DOWN] , &neighbours_ranks[UP]);

  // Creamos el tipo para cuando mandemos columnas a la derecha e izquierda
  MPI_Datatype columna;
  MPI_Type_vector( N , 1 , ld , MPI_DOUBLE , &columna);
  MPI_Type_commit( &columna);

  // Envío de columnas
  if (rank%2 == 0){
    MPI_Send( &x[1*ld+M] , 1 , columna , neighbours_ranks[RIGHT] , 0 , *comm_cart);
    MPI_Recv( &x[1*ld+0] , 1 , columna , neighbours_ranks[LEFT] , 0 , *comm_cart , MPI_STATUS_IGNORE);
    MPI_Send( &x[1*ld+1] , 1 , columna , neighbours_ranks[LEFT] , 0 , *comm_cart);
    MPI_Recv( &x[1*ld+M+1] , 1 , columna , neighbours_ranks[RIGHT] , 0 , *comm_cart , MPI_STATUS_IGNORE);
  }
  else{
    MPI_Recv( &x[1*ld+0] , 1 , columna , neighbours_ranks[LEFT] , 0 , *comm_cart , MPI_STATUS_IGNORE);
    MPI_Send( &x[1*ld+M] , 1 , columna , neighbours_ranks[RIGHT] , 0 , *comm_cart);
    MPI_Recv( &x[1*ld+M+1] , 1 , columna , neighbours_ranks[RIGHT] , 0 , *comm_cart , MPI_STATUS_IGNORE);
    MPI_Send( &x[1*ld+1] , 1 , columna , neighbours_ranks[LEFT] , 0 , *comm_cart);
  }

  // Envío de filas
  if (rank%2 == 0){
    MPI_Send( &x[N*ld+1] , M , MPI_DOUBLE , neighbours_ranks[DOWN] , 0 , *comm_cart);
    MPI_Recv( &x[0*ld+1] , M , MPI_DOUBLE , neighbours_ranks[UP] , 0 , *comm_cart , MPI_STATUS_IGNORE);
    MPI_Send( &x[1*ld+1] , M , MPI_DOUBLE , neighbours_ranks[UP] , 0 , *comm_cart);
    MPI_Recv( &x[(N+1)*ld+1] , M , MPI_DOUBLE , neighbours_ranks[DOWN] , 0 , *comm_cart , MPI_STATUS_IGNORE);
  }
  else{
    MPI_Recv( &x[0*ld+1] , M , MPI_DOUBLE , neighbours_ranks[UP] , 0 , *comm_cart , MPI_STATUS_IGNORE);
    MPI_Send( &x[N*ld+1] , M , MPI_DOUBLE , neighbours_ranks[DOWN] , 0 , *comm_cart);
    MPI_Recv( &x[(N+1)*ld+1] , M , MPI_DOUBLE , neighbours_ranks[DOWN] , 0 , *comm_cart , MPI_STATUS_IGNORE);
    MPI_Send( &x[1*ld+1] , M , MPI_DOUBLE , neighbours_ranks[UP] , 0 , *comm_cart);
  }

  int i, j;
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
void jacobi_poisson(int N,int M,double *x,double *b, MPI_Comm * comm_cart)
{
  int i, j, k, ld=M+2, conv, maxit=10000;
  double *t, local_s, total_s, tol=1e-6;

  t = (double*)calloc((N+2)*(M+2),sizeof(double));

  k = 0;
  conv = 0;

  int rank;
  MPI_Comm_rank(*comm_cart, &rank);

  while (!conv && k<maxit) {

    /* calcula siguiente vector */
    jacobi_step(N,M,x,b,t, comm_cart);

    /* criterio de parada: ||x_{k}-x_{k+1}||<tol */
    local_s = 0.0;
    for (i=1; i<=N; i++) {
      for (j=1; j<=M; j++) {
        local_s += (x[i*ld+j]-t[i*ld+j])*(x[i*ld+j]-t[i*ld+j]);
      }
    }

    MPI_Allreduce( &local_s , &total_s , 1 , MPI_DOUBLE , MPI_SUM , *comm_cart);
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
  int i, j, N=40, M=40, ld;
  double *x, *b, *sol, h=0.01, f=1.5;

  /* Extracción de argumentos */
  if (argc > 1) { /* El usuario ha indicado el valor de N */
    if ((N = atoi(argv[1])) < 0) N = 40;
  }
  if (argc > 2) { /* El usuario ha indicado el valor de M */
    if ((M = atoi(argv[2])) < 0) M = 1;
  }


  MPI_Init( &argc , &argv);
  
  int size;
  MPI_Comm_size(MPI_COMM_WORLD , &size);

  int m,n;
  m = M/(size/2);
  n = N/(size/2);

  // Creación del comunicador cartesiano
  int dims[2] = {0,0};
  MPI_Dims_create( size , 2 , dims);
  int periods[2] = {0,0};
  int reorder = 1;

  MPI_Comm comm_cart;
  MPI_Cart_create( MPI_COMM_WORLD , 2 , dims , periods , reorder , &comm_cart);


  ld = m+2;  /* leading dimension */

  /* Reserva de memoria */
  x = (double*)calloc((n+2)*(m+2),sizeof(double));
  b = (double*)calloc((n+2)*(m+2),sizeof(double));

  /* Inicializar datos */
  for (i=1; i<=n; i++) {
    for (j=1; j<=m; j++) {
      b[i*ld+j] = h*h*f;  /* suponemos que la función f es constante en todo el dominio */
    }
  }

  /* Resolución del sistema por el método de Jacobi */
  jacobi_poisson(n,m,x,b,&comm_cart);


  /* Recogida de la soliución en máster */
  int rank;
  MPI_Comm_rank(comm_cart, &rank);
  int my_coords[2];
  MPI_Cart_coords(comm_cart, rank, 2, my_coords);
  printf("[MPI process %d] I am located at (%d, %d).\n", rank, my_coords[0],my_coords[1]);

  // Creamos tipo de dato bloque entero
  MPI_Datatype bloque, bloque_sol;
  MPI_Type_vector(n,m,ld,MPI_DOUBLE,&bloque);
  MPI_Type_commit(&bloque);
  MPI_Type_vector(n,m,M,MPI_DOUBLE,&bloque_sol);

  sol = (double*)calloc(N*M,sizeof(double));

  if (my_coords[1]==1)
    MPI_Gather(&x[1*ld+1], 1, bloque, &sol[0*M+m*my_coords[0]],1,bloque_sol,0,comm_cart);
  else
    MPI_Gather(&x[1*ld+1], 1, bloque, &sol[n*M+m*my_coords[0]],1,bloque_sol,0,comm_cart);


  

  /* Imprimir solución (solo para comprobación, eliminar en el caso de problemas grandes) */
  if (!rank){
    ld = M;
    for (i=1; i<=N; i++) {
      for (j=1; j<=M; j++) {
        printf("%g ", x[i*ld+j]);
      }
      printf("\n");
    }
  }
 

  MPI_Type_free(&bloque);
  MPI_Type_free(&bloque_sol);
  free(x);
  free(b);
  free(sol);

  MPI_Finalize();
  return 0;
}


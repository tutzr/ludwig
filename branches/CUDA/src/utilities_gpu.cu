/*****************************************************************************
 *
 * utilities_gpu.cu
 *  
 * Data management and other utilities for GPU adaptation of Ludwig
 * Alan Gray/ Alan Richardson 
 *
 *****************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <math.h>

#include "pe.h"
#include "utilities_gpu.h"
#include "util.h"
#include "model.h"
#include "timer.h"


#define GPUS_PER_NODE 4

/* external pointers to data on host*/
extern const double ma_[NVEL][NVEL];
extern const double mi_[NVEL][NVEL];
extern const double wv[NVEL];
extern const int cv[NVEL][3];
extern const double q_[NVEL][3][3];




double * ma_d;
double * mi_d;
double * d_d;
int * cv_d;
double * q_d;
double * wv_d;
char * site_map_status_d;
double * force_d;
double * velocity_d;
int * N_d;
double * force_global_d;

/* host memory address pointers for temporary staging of data */

char * site_map_status_temp;
double * force_temp;
double * velocity_temp;

/* data size variables */
static int nhalo;
static int nsites;
static int ndist;
static  int N[3];
static  int Nall[3];


/* Perform tasks necessary to initialise accelerator */
void initialise_gpu()
{

  double force_global[3];

  int ic,jc,kc,index;
  

  int devicenum=cart_rank()%GPUS_PER_NODE;

  //FERMI0 hack
  if (devicenum ==1 ) devicenum=4;

  cudaSetDevice(devicenum);

  //cudaGetDevice(&devicenum);
  //printf("rank %d running on device %d\n",cart_rank(),devicenum);
  


  calculate_data_sizes();
  allocate_memory_on_gpu();

  /* get global force from physics module */
  fluid_body_force(force_global);

  /* get temp host copies of force and site_map_status arrays */
  for (ic=0; ic<=N[X]; ic++)
    {
      for (jc=0; jc<=N[Y]; jc++)
	{
	  for (kc=0; kc<=N[Z]; kc++)
	    {
	      index = coords_index(ic, jc, kc); 

	      site_map_status_temp[index] = site_map_get_status(ic, jc, kc);
	     

	    }
	}
    }


  /* copy data from host to accelerator */
  cudaMemcpy(N_d, N, 3*sizeof(int), cudaMemcpyHostToDevice); 
  cudaMemcpy(ma_d, ma_, NVEL*NVEL*sizeof(double), cudaMemcpyHostToDevice);
  cudaMemcpy(mi_d, mi_, NVEL*NVEL*sizeof(double), cudaMemcpyHostToDevice);
  cudaMemcpy(d_d, d_, 3*3*sizeof(double), cudaMemcpyHostToDevice); 
  cudaMemcpy(cv_d, cv, NVEL*3*sizeof(int), cudaMemcpyHostToDevice); 
  cudaMemcpy(wv_d, wv, NVEL*sizeof(double), cudaMemcpyHostToDevice); 
  cudaMemcpy(q_d, q_, NVEL*3*3*sizeof(double), cudaMemcpyHostToDevice); 
  cudaMemcpy(force_global_d, force_global, 3*sizeof(double), \
	     cudaMemcpyHostToDevice);
  cudaMemcpy(site_map_status_d, site_map_status_temp, nsites*sizeof(char), \
	     cudaMemcpyHostToDevice);
  

  init_dist_gpu();
  init_phi_gpu();


  //checkCUDAError("Init GPU");  


}

/* Perform tasks necessary to finalise accelerator */
void finalise_gpu()
{


  free_memory_on_gpu();
  finalise_dist_gpu();
  finalise_phi_gpu();
  checkCUDAError("finalise");


}




/* calculate sizes of data - needed for memory copies to accelerator */
static void calculate_data_sizes()
{
  coords_nlocal(N);  
  nhalo = coords_nhalo();  
  ndist = distribution_ndist();

  Nall[X]=N[X]+2*nhalo;
  Nall[Y]=N[Y]+2*nhalo;
  Nall[Z]=N[Z]+2*nhalo;

  nsites = Nall[X]*Nall[Y]*Nall[Z];



}





/* Allocate memory on accelerator */
static void allocate_memory_on_gpu()
{

  /* temp arrays for staging data on  host */
  force_temp = (double *) malloc(nsites*3*sizeof(double));
  velocity_temp = (double *) malloc(nsites*3*sizeof(double));
  site_map_status_temp = (char *) malloc(nsites*sizeof(char));
  
  cudaMalloc((void **) &site_map_status_d, nsites*sizeof(char));
  cudaMalloc((void **) &ma_d, NVEL*NVEL*sizeof(double));
  cudaMalloc((void **) &mi_d, NVEL*NVEL*sizeof(double));
  cudaMalloc((void **) &d_d, 3*3*sizeof(double));
  cudaMalloc((void **) &cv_d, NVEL*3*sizeof(int));
  cudaMalloc((void **) &wv_d, NVEL*sizeof(double));
  cudaMalloc((void **) &q_d, NVEL*3*3*sizeof(double));
  cudaMalloc((void **) &force_d, nsites*3*sizeof(double));
  cudaMalloc((void **) &velocity_d, nsites*3*sizeof(double));
  cudaMalloc((void **) &N_d, sizeof(int)*3);
  cudaMalloc((void **) &force_global_d, sizeof(double)*3);

  //   checkCUDAError("allocate_memory_on_gpu");

}


/* Free memory on accelerator */
static void free_memory_on_gpu()
{

  /* free temp memory on host */
  free(force_temp);
  free(velocity_temp);
  free(site_map_status_temp);

  cudaFree(ma_d);
  cudaFree(mi_d);
  cudaFree(d_d);
  cudaFree(cv_d);
  cudaFree(wv_d);
  cudaFree(q_d);
  cudaFree(site_map_status_d);
  cudaFree(force_d);
  cudaFree(velocity_d);
  cudaFree(N_d);
  cudaFree(force_global_d);

}

/* copy force from host to accelerator */
void put_force_on_gpu()
{

  int index, i, ic, jc, kc;
  double force[3];
	      

  /* get temp host copies of arrays */
  for (ic=1; ic<=N[X]; ic++)
    {
      for (jc=1; jc<=N[Y]; jc++)
	{
	  for (kc=1; kc<=N[Z]; kc++)
	    {
	      index = coords_index(ic, jc, kc); 

	      hydrodynamics_get_force_local(index,force);
	      	      
	      for (i=0;i<3;i++)
		{
		  force_temp[index*3+i]=force[i];
		}
	    }
	}
    }


  /* copy data from CPU to accelerator */
  cudaMemcpy(force_d, force_temp, nsites*3*sizeof(double), \
	     cudaMemcpyHostToDevice);

  //checkCUDAError("put_force_on_gpu");

}


void get_velocity_from_gpu()
{
  int index,i, ic,jc,kc; 
  double velocity[3];

  cudaMemcpy(velocity_temp, velocity_d, nsites*3*sizeof(double), 
	    cudaMemcpyDeviceToHost);
  //checkCUDAError("get_velocity_from_gpu");

  /* copy velocity from temporary array back to hydrodynamics module */
  for (ic=1; ic<=N[X]; ic++)
    {
      for (jc=1; jc<=N[Y]; jc++)
	{
	  for (kc=1; kc<=N[Z]; kc++)
	    {
	      index = coords_index(ic, jc, kc); 
	      for (i=0;i<3;i++)
		{		 
		  velocity[i]=velocity_temp[index*3+i];
		}     
	      hydrodynamics_set_velocity(index,velocity);
	    }
	}
    }

}



/* get linear index from 3d coordinates (host) */
int get_linear_index(int ii,int jj,int kk,int N[3])

{
  
  int yfac = N[Z];
  int xfac = N[Y]*yfac;

  return ii*xfac + jj*yfac + kk;

}


/* check for CUDA errors */
void checkCUDAError(const char *msg)
{
	cudaError_t err = cudaGetLastError();
	if( cudaSuccess != err) 
	{
		fprintf(stderr, "Cuda error: %s: %s.\n", msg, 
				cudaGetErrorString( err) );
		fflush(stdout);
		fflush(stderr);
		exit(EXIT_FAILURE);
	}                         
}
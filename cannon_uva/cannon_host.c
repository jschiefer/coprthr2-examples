
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>
#include <dlfcn.h>

#include "coprthr.h"
#include "coprthr_cc.h"
//#include "coprthr_thread.h"
#include "coprthr_mpi.h"

float f2(int n) {
	float x = (float)n;
	return (x*(x+1.0f)*(2.0f*x+1.0f)/6.0f);
}

float f3(int n) {
	float x = (float)n;
	float tmp = x*(x+1.0f)/2.0f;
	return (tmp*tmp);
}

float f4(int n) {
	float x = (float)n;
	return (x*(x+1.0f)*(2.0f*x+1.0f)*(3.0f*x*x+3.0f*x-1.0f)/30.0f);
}

struct my_args { int n; int s; int d; float* a; float* b; float* c; };

int main(int argc, char* argv[])
{

	int i,j,k;
	int row;
	struct timeval t0,t1;
	double time = 0.0;
	int n = SIZE;
	int s = SIZE2;
	int s2 = SIZE3;
	int d = EDIM;
	int v = 0;

	i = 1;
	while (i < argc) {
		if (!strcmp(argv[i],"-n")) n = atoi(argv[++i]);
		else if (!strcmp(argv[i],"-s")) s = atoi(argv[++i]);
		else if (!strcmp(argv[i],"-s2")) s2 = atoi(argv[++i]);
		else if (!strcmp(argv[i],"-d")) d = atoi(argv[++i]);
		else if (!strcmp(argv[i],"-v")) v = 1;
		else if (!strcmp(argv[i],"--help") || !strcmp(argv[i],"-h")) goto help;
		else {
			fprintf(stderr,"unrecognized option: %s\n",argv[i]);
			help:
			fprintf(stderr,"use -n [on-chip matrix dimension]"
				" -s [off-chip scale factor] -s2 [host scale factor]"
				" -d [eCore dimension]\n");
			exit(1);
		}
		++i;
	}

	printf("main: Using -n=%d, -s=%d, -s2=%d, -d=%d\n", n, s, s2, d);
	int N = s*s2*n;
	int nn = n*n;
	int ss = s*s;

	/* open device for threads */
	int dd = coprthr_dopen(COPRTHR_DEVICE_E32,COPRTHR_O_THREAD);
	printf("main: dd=%d\n",dd); fflush(stdout);
	if (dd<0) {
		printf("device open failed\n");
		exit(-1);
	}


	/* compile thread function */
	char* log = 0;
	coprthr_program_t prg = coprthr_cc_read_bin("./cannon_tfunc.e32",0);
	coprthr_sym_t thr = coprthr_getsym(prg,"my_thread");
	printf("main: %p %p\n",prg,thr);

	/* allocate host memory */
	float* aa = (float*)malloc(N*N*sizeof(float));
	float* bb = (float*)malloc(N*N*sizeof(float));
	float* cc = (float*)malloc(N*N*sizeof(float));

	/* allocate memory on device and write a value */
	size_t size_device_mem = s*s*n*n*sizeof(float);
	coprthr_mem_t tmpa_mem = coprthr_dmalloc(dd,size_device_mem,0);
	coprthr_mem_t tmpb_mem = coprthr_dmalloc(dd,size_device_mem,0);
	coprthr_mem_t tmpc_mem = coprthr_dmalloc(dd,size_device_mem,0);

//	float* tmpa = (float*)malloc(size_device_mem);
//	float* tmpb = (float*)malloc(size_device_mem);
//	float* tmpc = (float*)malloc(size_device_mem);
	float* tmpa = (float*)coprthr_memptr(tmpa_mem,0);
	float* tmpb = (float*)coprthr_memptr(tmpb_mem,0);
	float* tmpc = (float*)coprthr_memptr(tmpc_mem,0);
	
	struct my_args args = {
		.n = n, .s = s, .d = d,
//		.ga = coprthr_memptr(ga_mem,0),
//		.gb = coprthr_memptr(gb_mem,0),
//		.gc = coprthr_memptr(gc_mem,0)};
		.a = tmpa,
		.b = tmpb,
		.c = tmpc};

	/* initialize A, B, and C matrices */
	for (i=0; i<N; i++) {
		for (j=0; j<N; j++) {
			float x = (float)i + 1.0f;
			float y = (float)j + 1.0f;
			aa[i*N + j] = (float)(x*y*y - 2.0f*x*x*y);
			bb[i*N + j] = (float)(x*y*y + 2.0f*x*x*y);
			cc[i*N + j] = 0.0f;
		}
	}

	gettimeofday(&t0,0);
	time = 0;

	for(i=0;i<s2;i++) {
		for(j=0;j<s2;j++) {
			/* write data from host to device */
			size_t row_size = s*n*sizeof(float);
			int nrows = s*n;
			for(row=0;row<nrows;row++) {
//				memcpy(tmpc + row*n*s, gc + ((i*nrows+row)*s2 + j)*nrows,row_size);
				memcpy(tmpc + row*n*s, cc + ((i*nrows+row)*s2 + j)*nrows,row_size);
			}
//			coprthr_dwrite(dd, gc_mem, 0, tmpc, nrows*row_size, COPRTHR_E_WAIT);
			
			for(k=0;k<s2;k++) {
				for(row=0;row<nrows;row++) {
//					memcpy(tmpa+row*n*s, ga+((i*nrows+row)*s2 + k)*nrows, row_size);
//					memcpy(tmpb+row*n*s, gb+((k*nrows+row)*s2 + j)*nrows, row_size);
					memcpy(tmpa+row*n*s, aa+((i*nrows+row)*s2 + k)*nrows, row_size);
					memcpy(tmpb+row*n*s, bb+((k*nrows+row)*s2 + j)*nrows, row_size);
				}
//				coprthr_dwrite(dd, ga_mem, 0, tmpa, nrows*row_size, COPRTHR_E_WAIT);
//				coprthr_dwrite(dd, gb_mem, 0, tmpb, nrows*row_size, COPRTHR_E_WAIT);



				/* execute thread function */
				coprthr_mpiexec( dd, d*d, thr, &args, sizeof(struct my_args),0 );
			}
//			coprthr_dread(dd, gc_mem, 0, tmpc, nrows*row_size, COPRTHR_E_WAIT);
			for(row=0;row<nrows;row++) {
//				memcpy(gc+((i*nrows+row)*s2 + j)*nrows, tmpc+row*n*s, row_size);
				memcpy(cc+((i*nrows+row)*s2 + j)*nrows, tmpc+row*n*s, row_size);
			}
		}
	}

	gettimeofday(&t1,0);
	time += t1.tv_sec - t0.tv_sec + 1e-6*(t1.tv_usec - t0.tv_usec);

	int errors = 0;
	for(i=0;i<N;i++) {
		for(j=0;j<N;j++) {
			float x = (float)i + 1.0f;
			float y = (float)j + 1.0f;
			float d = (2.0f*x*y*f4(N) + (x*y*y-4.0f*x*x*y)*f3(N) 
				- 2.0f*x*x*y*y*f2(N));
			if (v) printf("[%d,%d]: %f %f %f %f\n",
				i,j, aa[i*N+j], bb[i*N+j], cc[i*N+j], d);
			float e = cc[i*N+j];
			float diff = fabsf(d/e - 1.0f);
			if(diff > 0.01f || isnan(e)) errors++;
		}
	}
	printf("main: mpiexec time %f sec\n",time);
	printf("main: # errors: %d\n", errors);

	/* clean up */
	coprthr_dfree(dd,tmpa_mem);
	coprthr_dfree(dd,tmpb_mem);
	coprthr_dfree(dd,tmpc_mem);

	coprthr_dclose(dd);
}
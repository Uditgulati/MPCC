//Pearsons correlation coeficient for two vectors x and y
// r = sum_i( x[i]-x_mean[i])*(y[i]-y_mean[i]) ) /
//     [ sqrt( sum_i(x[i]-x_mean[i])^2 ) sqrt(sum_i(y[i]-y_mean[i])^2 ) ]

// Matrix reformulation and algebraic simplification for improved algorithmic efficiency
// where A is matrix of X vectors and B is transposed matrix of Y vectors:
// R = [N sum(AB) - (sumA)(sumB)] /
//     sqrt[ (N sumA^2 - (sum A)^2)[ (N sumB^2 - (sum B)^2) ]

//This code computes correlation coefficient between all row/column pairs of two matrices 
// ./MPCC MatA_filename MatB_filename 

#include "MPCC.h"
#include "cblas.h"

using namespace std;

#define __assume(cond) do { if (!(cond)) __builtin_unreachable(); } while (0)
#define __assume_aligned(var,size){ __builtin_assume_aligned(var,size); }
#define DEV_CHECKPT printf("Checkpoint: %s, line %d\n", __FILE__, __LINE__); fflush(stdout); 

#ifndef NAIVE //default use matrix version
  #define NAIVE 0
#endif

#ifndef DOUBLE //default to float type
  #define DOUBLE 0
#endif

static DataType TimeSpecToSeconds(struct timespec* ts){
  return (DataType)ts->tv_sec + (DataType)ts->tv_nsec / 1000000000.0;
}

static DataType TimeSpecToNanoSeconds(struct timespec* ts){
  return (DataType)ts->tv_sec*1000000000.0 + (DataType)ts->tv_nsec;
}

// This function convert a string to datatype (double or float);
DataType convert_to_val(string text)
{
    DataType val;
    if(text=="nan" || text=="NaN" || text=="NAN"){ val = NANF;}
    else{ val = atof(text.c_str());}
    return val;
}

#ifndef USING_R

// This function initialized the matrices for m, n, p sized A and the B and result (C) matrices
// Not part of the R interface since R initializes the memory
void initialize(int &m, int &n, int &p, int seed,
    DataType **A, 
    DataType **B,
    DataType **C,
    char* matA_filename,
    char* matB_filename,
    bool &transposeB)
{
  // A is m x n (tall and skinny) row major order
  // B is n x p (short and fat) row major order
  // C, P is m x p (big and square) row major order

  //if matA_filename exists, read in dimensions
  // check for input file(s)
  std::string text;
  //DataType val;
  fstream mat_A_file;

  mat_A_file.open(matA_filename,ios::in);
  if(mat_A_file.is_open()){
     // if found then read
     std::getline(mat_A_file, text);
     m = convert_to_val(text);
     std::getline(mat_A_file, text);
     n = convert_to_val(text);
     printf("m=%d n=%d\n",m,n);
     mat_A_file.close();
  }
  //else use default value for m,n

  *A = (DataType *)mkl_calloc( m*n,sizeof( DataType ), 64 ); 
  if (*A == NULL ) {
    printf( "\n ERROR: Can't allocate memory for matrix A. Aborting... \n\n");
    mkl_free(*A);
    exit (0);
  }

  //if matB_filename exists, read in dimensions
  // check for input file(s)
  fstream mat_B_file;
  int _n=n;
  mat_B_file.open(matB_filename,ios::in);
  if(mat_B_file.is_open()){
     // if found then read
     std::getline(mat_B_file, text);
     _n = convert_to_val(text);
     std::getline(mat_B_file, text);
     p = convert_to_val(text);
     printf("_n=%d p=%d\n",_n,p);
     mat_B_file.close();
  }

  //check to see if we need to transpose B
  transposeB=false;
  if(_n !=n && n==p){//then transpose matrix B
     p=_n;
     _n=n; 
     transposeB=true; 
     printf("Transposing B for computational efficiency in GEMMs\n");
     printf("transposed _n=%d p=%d\n",_n,p);
   }

  //check that inner dimension matches
  assert(n==_n);

  //else use default value for n,p
  *B = (DataType *)mkl_calloc( n*p,sizeof( DataType ), 64 );
  if (*B == NULL ) {
    printf( "\n ERROR: Can't allocate memory for matrix B. Aborting... \n\n");
    mkl_free(*B);
    exit (0);
  }

  printf("m=%d n=%d p=%d\n",m,n,p);

  *C = (DataType *)mkl_calloc( m*p,sizeof( DataType ), 64 ); 
  if (*C == NULL ) {
    printf( "\n ERROR: Can't allocate memory for matrix C. Aborting... \n\n");
    mkl_free(*C);
    exit (0);
  }
  
  __assume_aligned(A, 64);
  __assume_aligned(B, 64);
  __assume_aligned(C, 64);
  //__assume(m%16==0);
 
  //setup random numbers to create some synthetic matrices for correlation
  // if input files do not exist
  srand(seed);
  DataType randmax_recip=1/(DataType)RAND_MAX;

  //Input currently hard coded as binary files matrixA.dat and matrixB.dat
  //Or text files input as command line parameters.
  //with the form:
  //int rows, cols 
  //float elems[rows:cols]

  //If input files do not exist, generate synthetic matrices of given dimensions A[m,p] B[p,n] 
  //  with randomly assigned elements from [0,1] and then add missing values 
  //  in selected locations (currently denoted by the value 2) 
  //These matrices are of type single precision floating point values
  
  // check for input file(s)
  mat_A_file.open(matA_filename,ios::in);
  if(mat_A_file.is_open()){
     // if found then read
     std::getline(mat_A_file, text);
     //m = convert_to_val(text);
     std::getline(mat_A_file, text);
     //n = convert_to_val(text);
     for(int i=0;i<m*n;++i){ 
        std::getline(mat_A_file, text);
        (*A)[i] = convert_to_val(text);
        //if(isnan((*A)[i])){printf("A[%d]==NAN\n",i);} 
     }
     mat_A_file.close();
  }
  else{ //else compute and then write matrix A
    //random assignemnt of threads gives inconsistent values, so keep serial
    int i;
    #pragma omp parallel for private (i)
    for (i=0; i<m*n; i++) {
      (*A)[i]=(DataType)rand()*randmax_recip;
    }
    //add some missing value markers
    //Note edge case: if missing data causes number of pairs compared to be <2, the result is divide by zero
    (*A)[0]          = MISSING_MARKER;
    (*A)[m*n-1]      = MISSING_MARKER;
    (*A)[((m-1)*n-1)]= MISSING_MARKER;

    //write matrix to file
    mat_A_file.open(matA_filename,ios::out);
    mat_A_file << m;
    mat_A_file << n;
    for(int i=0;i<m*n;++i) mat_A_file << (*A)[i] << '\n';
    mat_A_file.close();
  }
 
  //ceb Should write  out matrix and read in for future use.
  // check for input file(s)
  mat_B_file.open(matB_filename,ios::in);
  if(mat_B_file.is_open()){
     std::getline(mat_B_file, text);
     //m = convert_to_val(text);
     std::getline(mat_B_file, text);
     //n = convert_to_val(text);
     for(int i=0;i<n*p;++i){
        std::getline(mat_B_file, text);
        (*B)[i] = convert_to_val(text);
        //if(isnan((*B)[i]) ){printf("B[%d]==NAN\n",i);}
     }
     mat_B_file.close();
  }
  else{ //else compute and then write matrix B
    int i;
    //random assignemnt of threads gives inconsistent values, so keep serial
    #pragma omp parallel for private (i)
    for (i=0; i<n*p; i++) {
      (*B)[i]=(DataType)rand()*randmax_recip;
    }
    //add some missing value markers
    //ceb if missing data causes number of pairs compared to be <2, the result is divide by zero
    (*B)[0]          = MISSING_MARKER;
    (*B)[n*p-1]      = MISSING_MARKER;
    (*B)[((n-1)*p-1)]= MISSING_MARKER;
   
    //write matrix to file
    mat_B_file.open(matB_filename,ios::out);
    mat_B_file << n;
    mat_B_file << p;
    for(int i=0; i<n*p; ++i) mat_B_file << (*B)[i];
    mat_B_file.close();
  }
#if 0
  for (int i=0; i<m; i++) { for(int j=0;j<n;++j){printf("A[%d,%d]=%e\n",i,j,(*A)[i*n+j]);}}
  for (int i=0; i<n; i++) { for(int j=0;j<p;++j){printf("B[%d,%d]=%e\n",i,j,(*B)[i*p+j]);}}
#endif
  return;
};

#endif

void print_matrix(DataType* mat, int rows, int cols) {
  for(int i = 0; i < rows; i++) {
    for(int j = 0; j < cols; j++) {
      printf("%f ", mat[cols*i + j]);
    }
    printf("\n");
  }
}


//This function is the implementation of a matrix x matrix algorithm which computes a matrix of PCC values
//but increases the arithmetic intensity of the naive pairwise vector x vector correlation
//A is matrix of X vectors and B is transposed matrix of Y vectors:
//P = [ sum(AB) - (sumA)(sumB)/N] /
//    sqrt[ ( sumA^2 -(1/N) (sum A/)^2)[ ( sumB^2 - (1/N)(sum B)^2) ]
int pcc_matrix(int m, int n, int p,
               DataType* A, DataType* B, DataType* P)
{
  int i,j,k;
  DataType alpha=1.0;
  DataType beta=0.0;
  int count =1;
  bool transposeB = true; //assume this is always true. 
  //info("before calloc\n",1);
  //allocate and initialize and align memory needed to compute PCC
  DataType* N =     ( DataType*)calloc( m*p,sizeof(DataType) );
  DataType* SA =    ( DataType*)calloc( m*p, sizeof(DataType) );
  DataType* AA =    ( DataType*)calloc( m*n, sizeof(DataType) );
  DataType* SAA =   ( DataType*)calloc( m*p, sizeof(DataType) );
  DataType* SB =    ( DataType*)calloc( m*p, sizeof(DataType) );
  DataType* BB =    ( DataType*)calloc( n*p, sizeof(DataType) );
  DataType* SBB =   ( DataType*)calloc( m*p, sizeof(DataType) );
  DataType* SAB =   ( DataType*)calloc( m*p, sizeof(DataType) );
  DataType* UnitA = ( DataType*)calloc( m*n, sizeof(DataType) );
  DataType* UnitB = ( DataType*)calloc( n*p, sizeof(DataType) );
  DataType* amask=  ( DataType*)calloc( m*n, sizeof(DataType) );
  DataType* bmask=  ( DataType*)calloc( n*p, sizeof(DataType) );

  //info("after calloc\n",1);

  //if any of the above allocations failed, then we have run out of RAM on the node and we need to abort
  if ( (N == NULL) | (SA == NULL) | (AA == NULL) | (SAA == NULL) | (SB == NULL) | (BB == NULL) | 
      (SBB == NULL) | (SAB == NULL) | (UnitA == NULL) | (UnitB == NULL) | (amask == NULL) | (bmask == NULL)) {
    printf( "\n ERROR: Can't allocate memory for intermediate matrices. Aborting... \n\n");
    free(N);
    free(SA);
    free(AA);
    free(SAA);
    free(SB);
    free(BB);
    free(SBB);
    free(SAB);
    free(UnitA);
    free(UnitB);
    free(amask);
    free(bmask);
    #ifndef USING_R
    exit (0);
    #else
    return(0);
    #endif
  } 

  //info("before deal missing data\n",1);

  //deal with missing data
  for (int ii=0; ii<count; ii++) {

    //if element in A is missing, set amask and A to 0
    #pragma omp parallel for private (i,k)
    for (i=0; i<m; i++) {
      for (k=0; k<n; k++) {
        amask[ i*n + k ] = 1.0;
        if (CHECKNA(A[i*n+k])) { 
          amask[i*n + k] = 0.0;
          A[i*n + k] = 0.0; // set A to 0.0 for subsequent calculations of PCC terms
        }else{
          UnitA[i*n + k] = 1.0;
        }
      }
    }

    //if element in B is missing, set bmask and B to 0
    #pragma omp parallel for private (j,k)
    for (j=0; j<p; j++) {
      for (k=0; k<n; k++) {
        bmask[ j*n + k ] = 1.0;
        if (CHECKNA(B[j*n+k])) { 
          bmask[j*n + k] = 0.0;
          B[j*n + k] = 0.0; // set B to 0.0 for subsequent calculations of PCC terms
        }else{
          UnitB[j*n + k] = 1.0;
        }
      }
    }

    // GEMM(NoTrans, CblasTrans,
    //     m, p, n, alpha, amask, n, bmask, n, beta, N, p);
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
         m, p, n, alpha, amask, n, bmask, n, beta, N, p);

    printf("N\n");
    print_matrix(N, m, p);


    //vsSqr(m*n,A,AA);
    // VSQR(m*n,A,AA);
    printf("AA\n");
    #pragma omp parallel for private(i)
    for(int i = 0; i < m*n; i++) {
      AA[i] = A[i] * A[i];
      printf("%f ", AA[i]);
    }
    printf("\n");

    //vsSqr(n*p,B,BB);
    // VSQR(n*p,B,BB);
    printf("BB\n");
    #pragma omp parallel for private(i)
    for(int i = 0; i < n*p; i++) {
      BB[i] = B[i] * B[i];
      printf("%f ", BB[i]);
    }
    printf("\n");

    //variables used for performance timing
    //struct timespec startGEMM, stopGEMM;
    //double accumGEMM;

    //info("before PCC terms\n",1);

    //Compute PCC terms and assemble
     

    CBLAS_TRANSPOSE transB=CblasNoTrans;
    int ldb=p;
    if(transposeB){
      transB=CblasTrans;
      ldb=n;
    }

    //clock_gettime(CLOCK_MONOTONIC, &startGEMM);
    
    //SA = A*UnitB
    //Compute sum of A for each AB row col pair.
    // This requires multiplication with a UnitB matrix which acts as a mask 
    // to prevent missing data in AB pairs from contributing to the sum
    //cblas_sgemm(NoTrans, transB,
    cblas_dgemm(CblasRowMajor, CblasNoTrans, transB,
         m, p, n, alpha, A, n, UnitB, ldb, beta, SA, p); 

    printf("SA\n");
    print_matrix(SA, m, p);

    //SB = B*UnitA
    //Compute sum of B for each AB row col pair.
    // This requires multiplication with a UnitA matrix which acts as a mask 
    // to prevent missing data in AB pairs from contributing to the sum
    //cblas_sgemm(NoTrans, transB,
    cblas_dgemm(CblasRowMajor, CblasNoTrans, transB,
         m, p, n, alpha, UnitA, n, B, ldb, beta, SB, p);

    printf("SB\n");
    print_matrix(SB, m, p);


    //SAA = AA*UnitB
    //Compute sum of AA for each AB row col pair.
    // This requires multiplication with a UnitB matrix which acts as a mask 
    // to prevent missing data in AB pairs from contributing to the sum
    //cblas_sgemm(NoTrans, transB,
    cblas_dgemm(CblasRowMajor, CblasNoTrans, transB,
         m, p, n, alpha, AA, n, UnitB, ldb, beta, SAA, p); 

    printf("SAA\n");
    print_matrix(SAA, m, p);

    //SBB = BB*UnitA
    //Compute sum of BB for each AB row col pair.
    // This requires multiplication with a UnitA matrix which acts as a mask 
    // to prevent missing data in AB pairs from contributing to the sum
    //cblas_sgemm(NoTrans, transB,
    cblas_dgemm(CblasRowMajor, CblasNoTrans, transB,
         m, p, n, alpha, UnitA, n, BB, ldb, beta, SBB, p); 
    
    printf("SBB\n");
    print_matrix(SBB, m, p);

    free(UnitA);
    free(UnitB);
    free(AA);
    free(BB);

    //SAB = A*B
    //cblas_sgemm(NoTrans, transB,
    cblas_dgemm(CblasRowMajor, CblasNoTrans, transB,
         m, p, n, alpha, A, n, B, ldb, beta, SAB, p); 
    
    printf("SAB\n");
    print_matrix(SAB, m, p);

    //clock_gettime(CLOCK_MONOTONIC, &stopGEMM);
    //accumGEMM =  (TimeSpecToSeconds(&stopGEMM)- TimeSpecToSeconds(&startGEMM));
    //printf("All(5) GEMMs (%e)s GFLOPs=%e \n", accumGEMM, 5*(2/1.0e9)*m*n*p/accumGEMM);

    DataType* SASB = ( DataType*)calloc( m*p,sizeof(DataType) );
    DataType* NSAB = ( DataType*)calloc( m*p,sizeof(DataType) ); //ceb
   
    DataType* SASA = ( DataType*)calloc( m*p,sizeof(DataType) ); 
    DataType* NSAA = ( DataType*)calloc( m*p,sizeof(DataType) ); //ceb
    
    DataType* SBSB = ( DataType*)calloc( m*p,sizeof(DataType) );    
    DataType* NSBB = ( DataType*)calloc( m*p,sizeof(DataType) ); //ceb   
    
    DataType* DENOM = ( DataType*)calloc( m*p,sizeof(DataType) );
    DataType* DENOMSqrt =( DataType*)calloc( m*p,sizeof(DataType) ); 

    //Compute and assemble composite terms

    //SASB=SA*SB
    // VMUL(m*p,SA,SB,SASB);
    #pragma omp parallel for private(i)
    for(int i = 0; i < m*p; i++) {
      SASB[i] = SA[i] * SB[i];
    }
    printf("SASB\n");
    print_matrix(SASB, m, p);
    //N*SAB
    // VMUL(m*p,N,SAB,NSAB); //ceb
    #pragma omp parallel for private(i)
    for(int i = 0; i < m*p; i++) {
      NSAB[i] = N[i] * SAB[i];
    }

    printf("NSAB\n");
    print_matrix(NSAB, m, p);

    //NSAB=(-1)NSAB+SASB  (numerator)
    // AXPY(m*p,(DataType)(-1), SASB,1, NSAB,1); //ceb
    #pragma omp parallel for private(i)
    for(int i = 0; i < m*p; i++) {
      NSAB[i] = SASB[i] - NSAB[i];
    }
    printf("NSAB\n");
    print_matrix(NSAB, m, p);

    //(SA)^2
    // VSQR(m*p,SA,SASA);
    #pragma omp parallel for private(i)
    for(int i = 0; i < m*p; i++) {
      SASA[i] = SA[i] * SA[i];
    }
    printf("SASA\n");
    print_matrix(SASA, m, p);
    //N(SAA)
    // VMUL(m*p,N,SAA,NSAA); //cebSCAL();
    #pragma omp parallel for private(i)
    for(int i = 0; i < m*p; i++) {
      NSAA[i] = N[i] * SAA[i];
    }
    printf("NSAA\n");
    print_matrix(NSAA, m, p);
    //NSAA=NSAA-SASA (denominator term 1)
    // AXPY(m*p,(DataType)(-1), SASA,1, NSAA,1);
    #pragma omp parallel for private(i)
    for(int i = 0; i < m*p; i++) {
      NSAA[i] = NSAA[i] - SASA[i];
    }
    printf("NSAA\n");
    print_matrix(NSAA, m, p);

    //(SB)^2
    // VSQR(m*p,SB,SBSB);
    #pragma omp parallel for private(i)
    for(int i = 0; i < m*p; i++) {
      SBSB[i] = SB[i] * SB[i];
    }
    printf("SBSB\n");
    print_matrix(SBSB, m, p);
    //N(SBB)
    // VMUL(m*p,N,SBB,NSBB);
    #pragma omp parallel for private(i)
    for(int i = 0; i < m*p; i++) {
      NSBB[i] = N[i] * SBB[i];
    }
    printf("NSBB\n");
    print_matrix(NSBB, m, p);
    //NSBB=NSBB-SBSB (denominatr term 2)
    // AXPY(m*p,(DataType)(-1), SBSB,1, NSBB,1);
    #pragma omp parallel for private(i)
    for(int i = 0; i < m*p; i++) {
      NSBB[i] = NSBB[i] * SBSB[i];
    }
    printf("NSBB\n");
    print_matrix(NSBB, m, p);

    //DENOM=NSAA*NSBB (element wise multiplication)
    // VMUL(m*p,NSAA,NSBB,DENOM);
    #pragma omp parallel for private(i)
    for(int i = 0; i < m*p; i++) {
      DENOM[i] = NSAA[i] * NSBB[i];
    }
    printf("DENOM\n");
    print_matrix(DENOM, m, p);
    #pragma omp parallel for private(i)
    for(int i=0;i<m*p;++i){
      if(DENOM[i]==0.){DENOM[i]=1;}//numerator will be 0 so to prevent inf, set denom to 1
    }
    printf("DENOM\n");
    print_matrix(DENOM, m, p);
    //sqrt(DENOM)
    // VSQRT(m*p,DENOM,DENOMSqrt);
    #pragma omp parallel for private(i)
    for(int i = 0; i < m*p; i++) {
      DENOMSqrt[i] = sqrt(DENOM[i]);
    }
    printf("DENOMSqrt\n");
    print_matrix(DENOMSqrt, m, p);
    //P=NSAB/DENOMSqrt (element wise division)
    // VDIV(m*p,NSAB,DENOMSqrt,P); 
    #pragma omp parallel for private(i)
    for(int i = 0; i < m*p; i++) {
      P[i] = NSAB[i] / DENOMSqrt[i];
    }  
    printf("P\n");
    print_matrix(P, m, p);

    free(SASB);
    free(NSAB);
    free(SASA);
    free(NSAA);
    free(SBSB);
    free(NSBB);
    free(DENOM);
    free(DENOMSqrt); 
  }

  free(N);
  free(SA);
  free(SAA);
  free(SB);
  free(SBB);
  free(SAB);

  return 0;
}

#ifdef PCC_VECTOR

//This function uses bit arithmetic to mask vectors prior to performing a number of FMA's
//The intention is to improve upon the matrix x matrix missing data PCC algorithm by reducing uneccessary computations
// and maximizing the use of vector register arithmetic.
//A is matrix of X vectors and B is transposed matrix of Y vectors:
//P = [ N*sum(AB) - (sumA)(sumB)] /
//    sqrt[ (N sumA^2 -(sum A)^2)[ ( N sumB^2 - (sum B)^2) ]
//P = (N*SAB - SA*SB)/Sqrt( (N*SAA - (SA)^2) * (N*SBB - (SB)^2)  )

int pcc_vector(int m, int n, int p,
	       DataType* A, DataType* B, DataType* P)	       
{
  int i,j,k;
  int stride = ((n-1)/64 +1);
  DataType alpha=1.0;
  DataType beta=0.0;
  int count =1;

  bool transposeB = true; //assume this is always true. 

  //we need to compute terms using FMA and masking and then assemble terms to 
  // construct the PCC value for each row column pair

  unsigned long zeros = 0;
  unsigned long ones = ~0; //bitwise complement of 0 is all 1's (maxint)

  //We wamt to perform operations on vectors of float or double precision values. 
  //By constructing a mask in the form of a vector of floats or doubles, in which 
  // the values we want to keep are masked by a number which translates to a 111..1 (i.e. maxint for 32 or 64 bit values),
  // and the values we want to ignore are masked by a 000..0 bit string, we can simply apply a bitwise AND
  // to get a resultant vector on which we can perform an FMA operation

  //We also want to use this bitmask to compute a sum of non-missing data for each row-column pair

  //We can create a bitmask for both A and B matrices by doing the following:
  //  Initialize an Amask and Bmask matrix to all 1 bit strings. While reading A or B, 
  //  where there is a Nan bit string in A or B, place a 0 bit string in the corresponding mask matrix

  //To compute the various terms, eg, sum of A, for i=1..m, j=1..p, SA[i,j] = Sum_k(A[i] & Bmask[j]) 
  // reduce (sum) operation will have to be hand coded via openMP parallel reduce with pragma simd inside loop
  // horizontal_add returns sum of components of vector but is not efficient. Breaks SIMD ideal
 


  //How to create Amask and Bmask
  //In the process of creating masks for A and B we can either replace Nans with 0 as we go in A and B, or we can then apply the masks to 
  // to the matrices after we compute them.

  // We can create a matrix mask starting with a mask matrix containing all maxint bit strings except where A has missing data in which case the mask will contain a zero.
  // We can set these mask values to zero in the appropriate locations by traversing A or B and applying the isnan function in an if statement. 
  // If false, then set Amask or Bmask respectively to ones at that location (masks initialized to zeros)
  // (there may be a more efficent way to do this)
  
  DataType* Amask = ( DataType*)mkl_calloc( m*n, sizeof(DataType), 64 );
  __assume_aligned(Amask, 64);
  for(i=0; i<m*n; ++i){
     if(isnan(A[i])){ A[i]=0;}
     else{Amask[i]=ones;}
  }

  DataType* Bmask = ( DataType*)mkl_calloc( n*p, sizeof(DataType), 64 );
  __assume_aligned(Bmask, 64);    
  for(i=0; i<p*n; ++i){
     if(isnan(B[i])){ B[i]=0;}
     else{Bmask[i]=ones;}
  }

  //The masks can then be used by employing a bitwise (AND) &, between the elements of the mask and the elements of the matrix or vector we want to mask. 
  // The elements masked with a zero bit string will return a zero, the elements masked with the 1 bit string will return the original element.

  
  //How to compute N?
  // N[i,j] contains the number of valid pairs (no Nan's) of elements when comparing rows A[i] and B[j].
  // If we apply a bitwise (AND) & to compare rows A[i] and B[j] the result will be a bit vector of 1's for all element 
  // comparisons that don't include Nan's and zero's elsewhere. To sum up the valid element pairs, we could simply loop over the 
  // resulting vector and count up the maxints.
  //(There may be a faster way to sum values for N using bit ops)

  //N contains the number of elements used in each row column PCC calculation (after missing values are removed)
  DataType *N = (DataType *) mkl_calloc( m*p,sizeof( DataType ), 64 );
  __assume_aligned(N, 64);
  for(i=0; i<m; ++i){
     for(j=0; j<p; ++j){
        for(k=0; k<n; ++k){
           //if( (Amask[i*n+k] & Bmask[j*n+k]) > 0  ){ N[i*p+j]++; }  
           unsigned long* A_bitstring = reinterpret_cast<unsigned long* >(&(Amask[i*n+k]));  
           unsigned long* B_bitstring = reinterpret_cast<unsigned long* >(&(Bmask[j*n+k]));
           unsigned long C_bitstring = *A_bitstring & *B_bitstring;
           if( C_bitstring > 0){ N[i*p+j]++;}
        }
     }
  }


  //After computing masks and N, we want to replace Nan's in A and B with 0.
  // We can do this by masking A with Amask and B with Bmask 

  //AA and BB can be computed simply by multiplying vector A[i] by vector A[i] and store in AA[i] via FMA, after A and B have had mask applied

  DataType tmp;
  
  //SAB may best be done by a GEMM operation, though the symmetric portion of the computation can be reduced by 
  //  eliminating the lower triangular redundancy.
  

  DataType* SAB =   ( DataType*)mkl_calloc( m*p, sizeof(DataType), 64 );
  __assume_aligned(SAB, 64);
  //#pragma omp parallel for reduction (+:sum)
  for(i=0; i<m; ++i){
    //for(j=i; j<p; ++j){//upper triangular computations only. Assuming p>=m
    //for(j=; j<p; ++j){//upper triangular computations only. Assuming p>=m
    for(j=0; j<p; ++j){
       //#pragma SIMD
       for(k=0;k<n;++k){
          SAB[i*n+j] += A[i*n+k]*B[j*n+k];
       }
    }
  }

  DataType* SA =   ( DataType*)mkl_calloc( m*p, sizeof(DataType), 64 );
  __assume_aligned(SA, 64);
  //#pragma omp parallel for reduction (+:sum)
  for(i=0; i<m; ++i){
    for(j=0; j<p; ++j){
       //#pragma SIMD
       for(k=0;k<n;++k){
          //tmp=(A[i*n+k] & Bmask[j*n+k]);
          unsigned long* A_bitstring = reinterpret_cast<unsigned long* >(&(A[i*n+k]));
          unsigned long* B_bitstring = reinterpret_cast<unsigned long* >(&(Bmask[j*n+k]));
          unsigned long C_bitstring = *A_bitstring & *B_bitstring;
          SA[i*n+j] += reinterpret_cast<DataType >(C_bitstring);
       }
    }
  }

  DataType* SB =   ( DataType*)mkl_calloc( m*p, sizeof(DataType), 64 );
  __assume_aligned(SB, 64);
  //#pragma omp parallel for reduction (+:sum)
  for(j=0; j<p; ++j){
     for(i=0; i<m; ++i){
       //#pragma SIMD
       for(k=0;k<n;++k){
          //tmp=(B[j*n+k] & Amask[i*n+k]);
           unsigned long* A_bitstring = reinterpret_cast<unsigned long* >(&(Amask[i*n+k]));
           unsigned long* B_bitstring = reinterpret_cast<unsigned long* >(&(B[j*n+k]));
           unsigned long C_bitstring = *A_bitstring & *B_bitstring;
          SB[j*n+k] += reinterpret_cast<DataType >(C_bitstring);
       }
    }
  }

  DataType* SAA =   ( DataType*)mkl_calloc( m*p, sizeof(DataType), 64 );
  __assume_aligned(SAA, 64);
  //#pragma omp parallel for reduction (+:sum)
  for(i=0; i<m; ++i){
    for(j=0; j<p; ++j){
       //#pragma SIMD
       for(k=0;k<n;++k){
          //tmp=(A[i*n+k] & Bmask[j*n+k]);
          unsigned long* A_bitstring = reinterpret_cast<unsigned long* >(&(Amask[i*n+k]));
          unsigned long* B_bitstring = reinterpret_cast<unsigned long* >(&(B[j*n+k]));
          unsigned long C_bitstring = *A_bitstring & *B_bitstring;
          tmp=reinterpret_cast<DataType >(C_bitstring);
          SAA[i*n+j] += tmp*tmp;
       }
    }
  }

  DataType* SBB =   ( DataType*)mkl_calloc( m*p, sizeof(DataType), 64 );
  __assume_aligned(SBB, 64);
  //#pragma omp parallel for reduction (+:sum)
  for(j=0; j<p; ++j){
     for(i=0; i<m; ++i){
       //#pragma SIMD
       for(k=0;k<n;++k){
          //tmp=(B[j*n+k] & Amask[i*n+k]);
          unsigned long* A_bitstring = reinterpret_cast<unsigned long* >(&(Amask[i*n+k]));
          unsigned long* B_bitstring = reinterpret_cast<unsigned long* >(&(B[j*n+k]));
          unsigned long C_bitstring = *A_bitstring & *B_bitstring;
          SBB[j*n+i] += reinterpret_cast<DataType >(C_bitstring);;
       }
    }
  }


  //allocate and initialize and align memory needed to compute PCC



    //variables used for performance timing
    struct timespec startGEMM, stopGEMM;
    double accumGEMM;

    //info("before PCC terms\n",1);

    //Compute PCC terms and assemble

    CBLAS_TRANSPOSE transB=CblasNoTrans;
    int ldb=p;
    if(transposeB){
      transB=CblasTrans;
      ldb=n;
    }

    clock_gettime(CLOCK_MONOTONIC, &startGEMM);
    mkl_free(UnitA);
    mkl_free(UnitB);

    clock_gettime(CLOCK_MONOTONIC, &stopGEMM);
    accumGEMM =  (TimeSpecToSeconds(&stopGEMM)- TimeSpecToSeconds(&startGEMM));
    //printf("All(5) GEMMs (%e)s GFLOPs=%e \n", accumGEMM, 5*(2/1.0e9)*m*n*p/accumGEMM);

    DataType* SASB = ( DataType*)mkl_calloc( m*p,sizeof(DataType), 64 );
    DataType* NSAB = ( DataType*)mkl_calloc( m*p,sizeof(DataType), 64 ); //ceb
   
    DataType* SASA = ( DataType*)mkl_calloc( m*p,sizeof(DataType), 64 ); 
    DataType* NSAA = ( DataType*)mkl_calloc( m*p,sizeof(DataType), 64 ); //ceb
    
    DataType* SBSB = ( DataType*)mkl_calloc( m*p,sizeof(DataType), 64 );    
    DataType* NSBB = ( DataType*)mkl_calloc( m*p,sizeof(DataType), 64 ); //ceb   
    
    DataType* DENOM = ( DataType*)mkl_calloc( m*p,sizeof(DataType), 64 );
    DataType* DENOMSqrt =( DataType*)mkl_calloc( m*p,sizeof(DataType), 64 ); 

    //Compute and assemble composite terms

    //SASB=SA*SB
    VMUL(m*p,SA,SB,SASB);
    //N*SAB
    VMUL(m*p,N,SAB,NSAB); //ceb
    //SAB=(-1)SASB+NSAB  (numerator)
    AXPY(m*p,(DataType)(-1), SASB,1, NSAB,1); //ceb

    //(SA)^2
    //vsSqr(m*p,SA,SASA);
    VSQR(m*p,SA,SASA);
    //N(SAA)
    VMUL(m*p,N,SAA,NSAA); //ceb
    //SAA=NSAA-SASA (denominator term 1)
    AXPY(m*p,(DataType)(-1), SASA,1, NSAA,1);

    //(SB)^2
    //vsSqr(m*p,SB,SBSB);
    VSQR(m*p,SB,SBSB);
    //N(SBB)
    VMUL(m*p,N,SBB,NSBB);
    //SBB=NSBB-SBSB
    AXPY(m*p,(DataType)(-1), SBSB,1, NSBB,1);

    //DENOM=NSAA*NSBB (element wise multiplication)
    VMUL(m*p,NSAA,NSBB,DENOM);
    for(int i=0;i<m*p;++i){
       if(DENOM[i]==0.){DENOM[i]=1;}//numerator will be 0 so to prevent inf, set denom to 1
    }
    //sqrt(DENOM)
    VSQRT(m*p,DENOM,DENOMSqrt);
    //P=SAB/DENOMSqrt (element wise division)
    VDIV(m*p,SAB,DENOMSqrt,P);   

    mkl_free(SASA);
    mkl_free(SASB);
    mkl_free(SBSB);
    mkl_free(NSAB);
    mkl_free(NSAA);
    mkl_free(NSBB);
    mkl_free(DENOM);
    mkl_free(DENOMSqrt); 
  }

  mkl_free(N);
  mkl_free(SA);
  mkl_free(SAA);
  mkl_free(SB);
  mkl_free(SBB);
  mkl_free(SAB);

  return 0;
};

#endif

#ifndef USING_R

int main (int argc, char **argv) {
  //ceb testing with various square matrix sizes
  //16384 = 1024*16
  //32768 = 2048*16
  //40960 = 2560*16 too large (for skylake)

  //set default values 
  int m=64;//16*1500;//24000^3 for peak performance on skylake
  int n=16;
  int p=32;
  int count=1;
  int seed =1; 
  char* matA_filename;//="matA.dat";
  char* matB_filename;//="matB.dat";
 
  if(argc>1){ matA_filename = argv[1]; }
  if(argc>2){ matB_filename = argv[2]; }
  
  struct timespec startPCC,stopPCC;
  // A is n x p (tall and skinny) row major order
  // B is p x m (short and fat) row major order
  // R is n x m (big and square) row major order
  DataType* A;
  DataType* B; 
  DataType* R;
  DataType* diff;
  DataType* C;
  DataType accumR;
   
  bool transposeB=false;
  initialize(m, n, p, seed, &A, &B, &R, matA_filename, matB_filename, transposeB);
  //C = (DataType *)mkl_calloc( m*p,sizeof( DataType ), 64 );
  clock_gettime(CLOCK_MONOTONIC, &startPCC);
#if NAIVE
  printf("naive PCC implmentation\n");
  pcc_naive(m, n, p, A, B, R);
#else  
  printf("matrix PCC implmentation\n");
  pcc_matrix(m, n, p, A, B, R);
  //pcc_vector(m, n, p, A, B, R);
#endif
  clock_gettime(CLOCK_MONOTONIC, &stopPCC);
  accumR =  (TimeSpecToSeconds(&stopPCC)- TimeSpecToSeconds(&startPCC));



#if 0
  //read in results file for comparison
  fstream test_file;
  //test_file.open("results_6k_x_29k_values.txt",ios::in);
  test_file.open("6kvs28k.txt",ios::in);
  //test_file.open("flat.txt",ios::in);
  if(test_file.is_open()){
     float tmp;
     // if found then read
     int dim1,dim2;
     test_file >> tmp; 
     dim1 = tmp;
     test_file >> tmp;
     dim2 = tmp;
     printf("dim1=%d dim2=%d dim1*dim2=%d\n",dim1,dim2,dim1*dim2);
     C = (DataType *)mkl_calloc( dim1*dim2,sizeof( DataType ), 64 );
     for(int i=0;i<dim1*dim2;++i) test_file >> C[i];
     test_file.close();
  }
#endif 

#if 0
    //write R matrix to file
    fstream mat_R_file;
    mat_R_file.open("MPCC_computed.txt",ios::out);
    mat_R_file << m << '\n';
    mat_R_file << p << '\n';
    for(int i=0;i<m*p;++i) mat_R_file << R[i] << '\n';
    mat_R_file.close();
#endif
 
  DataType R_2norm = 0.0;
  DataType C_2norm = 0.0;
  DataType diff_2norm = 0.0;
  DataType relativeNorm = 0.0;

#if 0
  for (int i=0; i<m*p; i++) { C_2norm += C[i]*C[i]; }
  C_2norm=sqrt(C_2norm);
  for (int i=0; i<m*p; i++) { R_2norm += R[i]*R[i]; }
  R_2norm=sqrt(R_2norm);
  diff = (DataType *)mkl_calloc( m*p,sizeof( DataType ), 64 );
  for (int i=0; i<m*p; i++) { 
     diff[i]=pow(C[i]-R[i],2);
     diff_2norm += diff[i]; 
  }

  diff_2norm = sqrt(diff_2norm);
  relativeNorm = diff_2norm/R_2norm;
  printf("R_2Norm=%e, C_2Norm=%e, diff_2norm=%e relativeNorm=%e\n", R_2norm, C_2norm, diff_2norm, relativeNorm);
  printf("relative diff_2Norm = %e in %e s m=%d n=%d p=%d GFLOPs=%e \n", relativeNorm, accumR, m,n,p, (5*2/1.0e9)*m*n*p/accumR);
#endif


#if 0
    //write R matrix to file
    fstream diff_file;
    diff_file.open("diff.txt",ios::out);
    diff_file << m << '\n';
    diff_file << p << '\n';
    for(int i=0;i<m*p;++i) diff_file << R[i] << " " << C[i] << " " <<diff[i] << '\n';
    diff_file.close();
#endif

  printf("completed in %e seconds, size: m=%d n=%d p=%d GFLOPs=%e \n",accumR, m,n,p, (5*2/1.0e9)*m*n*p/accumR);

  return 0;
}

#endif


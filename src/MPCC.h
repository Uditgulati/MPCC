/******************************************************************//**
 * \file MPCC.h
 * \brief Global definitions and includes
 *
 **********************************************************************/
#ifndef __MPCC_H__
  #define __MPCC_H__

    #define BILLION  1000000000L

    #ifdef STANDALONE // Completely standalone (TODO: Implement LIB)

      // #error "Completely standalone (TODO: export as R-bound DYNLIB)"
 
      #include <stdio.h>
      #include <stdlib.h>
      #include <stdint.h>
      #include <cfloat>

      #include <cmath>
      #include <iostream>
      #include <fstream>
      #include <time.h>
      #include <assert.h>

      #define info(format, ...) { \
        printf(format, __VA_ARGS__); \
        fflush(stdout); }
      #define err(format, ...) { \
        printf(format, __VA_ARGS__); \
        exit(-1); }
        
      #define CHECKNA std::isnan
        
    #else
      #define DOUBLE 1
      #include <R.h>
      #include <Rmath.h>
      #include <string>

      #define info(format, ...) { \
        Rprintf(format, __VA_ARGS__);}
      #define err(format, ...) { \
        error(format, __VA_ARGS__);}
      
      #define CHECKNA std::isnan
    #endif

  #if DOUBLE
    #define DataType double
  #else
    #define DataType float
  #endif

#ifdef __MINGW32__
    #define NANF nan("1")
#else
    #define NANF std::nan("1")
#endif
    
#define MISSING_MARKER NANF

    // Forward declaration of the functions
    int pcc_matrix(int m, int n, int p, DataType* A, DataType* B, DataType* P);
    int pcc_vector(int m, int n, int p, DataType* A, DataType* B, DataType* P);
    int pcc_naive(int m, int n, int p, DataType* A, DataType* B, DataType* P);

#endif //__MPCC_H__


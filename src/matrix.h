/* matrix.h -- dense matrices over GF(2^w). */
#ifndef MATRIX_H
#define MATRIX_H

#include <stdint.h>
#include <stddef.h>
#include "gf.h"

typedef struct {
    int       rows;
    int       cols;
    uint32_t *e;      /* row-major, rows*cols entries */
} gf_mat;

#define MAT(M, i, j)  ((M)->e[(size_t)(i) * (size_t)(M)->cols + (size_t)(j)])

int  mat_alloc(gf_mat *M, int rows, int cols);
void mat_free(gf_mat *M);
void mat_print(const gf_mat *M, const char *name);

/* u(M): number of nonzero elements -- the paper's cost metric (Sec. 2.3). */
long mat_nonzeros(const gf_mat *M);
/* v(M): number of elements greater than 1.  Unused by the Milestone 1 normal
 * sequence; it is what gPPM's C = u + c*v needs in Milestone 3. */
long mat_non_ones(const gf_mat *M);

/* Dense coefficient-matrix multiplication over GF(2^w).  This does not use
 * mult_XORs(), because the paper's C metric counts block-region operations. */
int  mat_mul(const gf_mat *A, const gf_mat *B, const gf_t *gf, gf_mat *out);

/* Gauss-Jordan inversion.  Returns 0 on success, -1 if A is singular. */
int  mat_invert(const gf_mat *A, const gf_t *gf, gf_mat *out);

#endif /* MATRIX_H */

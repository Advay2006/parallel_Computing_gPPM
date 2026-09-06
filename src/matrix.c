#include "matrix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mat_alloc(gf_mat *M, int rows, int cols)
{
    M->rows = rows;
    M->cols = cols;
    M->e = calloc((size_t)rows * cols, sizeof *M->e);
    return M->e ? 0 : -1;
}

void mat_free(gf_mat *M)
{
    free(M->e);
    M->e = NULL; M->rows = M->cols = 0;
}

void mat_print(const gf_mat *M, const char *name)
{
    int i, j;
    printf("%s (%dx%d):\n", name, M->rows, M->cols);
    for (i = 0; i < M->rows; i++) {
        printf("  [");
        for (j = 0; j < M->cols; j++) printf("%4u", MAT(M, i, j));
        printf(" ]\n");
    }
}

long mat_nonzeros(const gf_mat *M)
{
    long c = 0; size_t i, N = (size_t)M->rows * M->cols;
    for (i = 0; i < N; i++) if (M->e[i] != 0) c++;
    return c;
}

long mat_non_ones(const gf_mat *M)
{
    long c = 0; size_t i, N = (size_t)M->rows * M->cols;
    for (i = 0; i < N; i++) if (M->e[i] > 1) c++;
    return c;
}

int mat_invert(const gf_mat *A, const gf_t *gf, gf_mat *out)
{
    int n = A->rows, col, i, j, piv;
    gf_mat W;               /* [A | I], n x 2n */

    if (A->rows != A->cols) return -1;
    if (mat_alloc(&W, n, 2 * n) != 0) return -1;

    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) MAT(&W, i, j) = MAT(A, i, j);
        MAT(&W, i, n + i) = 1;
    }

    for (col = 0; col < n; col++) {
        /* find a pivot */
        piv = -1;
        for (i = col; i < n; i++)
            if (MAT(&W, i, col) != 0) { piv = i; break; }
        if (piv < 0) { mat_free(&W); return -1; }   /* singular */

        if (piv != col)
            for (j = 0; j < 2 * n; j++) {
                uint32_t t = MAT(&W, col, j);
                MAT(&W, col, j) = MAT(&W, piv, j);
                MAT(&W, piv, j) = t;
            }

        /* normalise the pivot row */
        if (MAT(&W, col, col) != 1) {
            uint32_t inv = gf_inv(gf, MAT(&W, col, col));
            for (j = 0; j < 2 * n; j++)
                MAT(&W, col, j) = gf_mul(gf, MAT(&W, col, j), inv);
        }

        /* eliminate the column from every other row */
        for (i = 0; i < n; i++) {
            uint32_t f = MAT(&W, i, col);
            if (i == col || f == 0) continue;
            for (j = 0; j < 2 * n; j++)
                MAT(&W, i, j) ^= gf_mul(gf, f, MAT(&W, col, j));
        }
    }

    if (mat_alloc(out, n, n) != 0) { mat_free(&W); return -1; }
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++) MAT(out, i, j) = MAT(&W, i, n + j);

    mat_free(&W);
    return 0;
}

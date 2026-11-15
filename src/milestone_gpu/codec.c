#include "codec.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <omp.h>

int ec_split(const gf_mat *H, const int *faulty, int nf,
             gf_mat *F, gf_mat *S, int *surviving)
{
    const int C = H->cols, R = H->rows;
    int i, j, k, ns;
    char *is_faulty;

    memset(F, 0, sizeof *F);
    memset(S, 0, sizeof *S);
    is_faulty = calloc((size_t)C, 1);
    if (!is_faulty) return -1;
    for (k = 0; k < nf; k++) {
        if (faulty[k] < 0 || faulty[k] >= C || is_faulty[faulty[k]]) {
            free(is_faulty); return -1;          /* out of range or duplicate */
        }
        is_faulty[faulty[k]] = 1;
    }

    ns = 0;
    for (j = 0; j < C; j++) if (!is_faulty[j]) surviving[ns++] = j;

    if (mat_alloc(F, R, nf) != 0) {
        free(is_faulty); return -1;
    }
    if (mat_alloc(S, R, ns) != 0) {
        mat_free(F); free(is_faulty); return -1;
    }
    for (i = 0; i < R; i++) {
        for (k = 0; k < nf; k++) MAT(F, i, k) = MAT(H, i, faulty[k]);
        for (k = 0; k < ns; k++) MAT(S, i, k) = MAT(H, i, surviving[k]);
    }

    free(is_faulty);
    return ns;
}

int ec_decode_normal(const gf_mat *Finv, const gf_mat *S,
                     const int *faulty, const int *surviving,
                     uint8_t *stripe, size_t sector_bytes, const gf_t *gf)
{
    const int R  = S->rows;      /* R_H                       */
    const int ns = S->cols;      /* surviving blocks          */
    const int nf = Finv->rows;   /* faulty blocks (= R here)  */
    uint8_t *T;                  /* R intermediate sectors    */
    int i, j;

    T = calloc((size_t)R, sector_bytes);
    if (!T) return -1;

    /* T = S * BS.  Cost: u(S) calls. */
    for (i = 0; i < R; i++)
        for (j = 0; j < ns; j++) {
            uint32_t a = MAT(S, i, j);
            if (a == 0) continue;              /* u() counts nonzeros only */
            mult_XORs(stripe + (size_t)surviving[j] * sector_bytes,
                      T + (size_t)i * sector_bytes, a, sector_bytes, gf);
        }

    /* BF = Finv * T.  Cost: u(Finv) calls.  Computed only after all of T is
     * final, so overwriting the faulty sectors in place is safe. */
    for (i = 0; i < nf; i++) {
        uint8_t *dst = stripe + (size_t)faulty[i] * sector_bytes;
        memset(dst, 0, sector_bytes);
        for (j = 0; j < R; j++) {
            uint32_t a = MAT(Finv, i, j);
            if (a == 0) continue;
            mult_XORs(T + (size_t)j * sector_bytes, dst, a, sector_bytes, gf);
        }
    }

    free(T);
    return 0;
}

int ec_decode_normal_parallel(const gf_mat *Finv, const gf_mat *S,
                              const int *faulty, const int *surviving,
                              uint8_t *stripe, size_t sector_bytes,
                              const gf_t *gf, int requested_threads,
                              int *actual_threads)
{
    int R, ns, nf;
    size_t symbol_bytes, symbols;
    uint8_t *T;
    int threads, observed_threads = 1;

    if (actual_threads) *actual_threads = 1;
    if (!Finv || !S || !faulty || !surviving || !stripe || !gf ||
        requested_threads <= 0 || Finv->rows != Finv->cols ||
        Finv->cols != S->rows || sector_bytes == 0 ||
        (gf->w != 8 && gf->w != 16 && gf->w != 32))
        return -1;
    R = S->rows;
    ns = S->cols;
    nf = Finv->rows;
    symbol_bytes = (size_t)(gf->w / 8);
    symbols = sector_bytes / symbol_bytes;
    if (sector_bytes % symbol_bytes != 0) return -1;
    if (requested_threads == 1 || symbols < 2)
        return ec_decode_normal(Finv, S, faulty, surviving, stripe,
                               sector_bytes, gf);

    threads = requested_threads;
    if ((size_t)threads > symbols) threads = (int)symbols;
    T = calloc((size_t)R, sector_bytes);
    if (!T) return -1;

    gf_count_reset();
#pragma omp parallel num_threads(threads) shared(T, observed_threads)
    {
        int tid = omp_get_thread_num();
        int team = omp_get_num_threads();
        int i, j;
        size_t first_symbol = symbols * (size_t)tid / (size_t)team;
        size_t last_symbol = symbols * (size_t)(tid + 1) / (size_t)team;
        size_t first_byte = first_symbol * symbol_bytes;
        size_t chunk_bytes = (last_symbol - first_symbol) * symbol_bytes;

#pragma omp single
        observed_threads = team;

        for (i = 0; i < R; i++)
            for (j = 0; j < ns; j++) {
                uint32_t a = MAT(S, i, j);
                if (a != 0)
                    mult_XORs(stripe + (size_t)surviving[j] * sector_bytes + first_byte,
                              T + (size_t)i * sector_bytes + first_byte,
                              a, chunk_bytes, gf);
            }

        for (i = 0; i < nf; i++) {
            uint8_t *dst = stripe + (size_t)faulty[i] * sector_bytes + first_byte;
            memset(dst, 0, chunk_bytes);
            for (j = 0; j < R; j++) {
                uint32_t a = MAT(Finv, i, j);
                if (a != 0)
                    mult_XORs(T + (size_t)j * sector_bytes + first_byte,
                              dst, a, chunk_bytes, gf);
            }
        }
    }

    free(T);
    /* Count logical full-region operations once, not one chunk per worker. */
    gf_count_reset();
    gf_count_add((uint64_t)mat_nonzeros(S) + (uint64_t)mat_nonzeros(Finv));
    if (actual_threads) *actual_threads = observed_threads;
    return 0;
}

int ec_decode_matrix_first(const gf_mat *Finv, const gf_mat *S,
                           const int *faulty, const int *surviving,
                           uint8_t *stripe, size_t sector_bytes,
                           const gf_t *gf)
{
    gf_mat G = { 0 };
    int i, j;

    if (!Finv || !S || !faulty || !surviving || !stripe || !gf ||
        Finv->rows != Finv->cols || Finv->cols != S->rows ||
        sector_bytes == 0 || sector_bytes % (size_t)(gf->w / 8) != 0)
        return -1;
    if (mat_mul(Finv, S, gf, &G) != 0) return -1;

    for (i = 0; i < G.rows; i++) {
        uint8_t *dst = stripe + (size_t)faulty[i] * sector_bytes;
        memset(dst, 0, sector_bytes);
        for (j = 0; j < G.cols; j++) {
            uint32_t a = MAT(&G, i, j);
            if (a == 0) continue;
            mult_XORs(stripe + (size_t)surviving[j] * sector_bytes,
                      dst, a, sector_bytes, gf);
        }
    }

    mat_free(&G);
    return 0;
}

int ec_choose_sequence(const gf_mat *Finv, const gf_mat *S, const gf_t *gf,
                       double c, decode_sequence_t *sequence,
                       double *estimated_cost)
{
    gf_mat G = { 0 };
    double normal_cost, matrix_cost;

    if (!Finv || !S || !gf || !sequence || !estimated_cost ||
        !Finv->e || !S->e || c < 0.0 || !isfinite(c) ||
        Finv->rows != Finv->cols || Finv->cols != S->rows)
        return -1;

    normal_cost = (double)mat_nonzeros(S) + (double)mat_nonzeros(Finv) +
                  c * ((double)mat_non_ones(S) + (double)mat_non_ones(Finv));
    if (mat_mul(Finv, S, gf, &G) != 0) return -1;
    matrix_cost = (double)mat_nonzeros(&G) + c * (double)mat_non_ones(&G);
    if (matrix_cost < normal_cost) {
        *sequence = DECODE_MATRIX_FIRST;
        *estimated_cost = matrix_cost;
    } else {
        *sequence = DECODE_NORMAL;
        *estimated_cost = normal_cost;
    }
    mat_free(&G);
    return 0;
}

int ec_recover(const gf_mat *H, const int *faulty, int nf,
               uint8_t *stripe, size_t sector_bytes, const gf_t *gf,
               decode_stats_t *stats)
{
    gf_mat F = { 0 }, S = { 0 }, Finv = { 0 };
    int   *surviving, ns, rc = -1;

    if (nf != H->rows || sector_bytes == 0 ||
        sector_bytes % (size_t)(gf->w / 8) != 0)
        return -1;                             /* F must be square */

    surviving = malloc(sizeof(int) * (size_t)(H->cols - nf));
    if (!surviving) return -1;

    ns = ec_split(H, faulty, nf, &F, &S, surviving);       /* Step 2 */
    if (ns < 0) { free(surviving); return -1; }

    if (mat_invert(&F, gf, &Finv) != 0) goto done;         /* Step 3 */

    gf_count_reset();                                      /* Step 4 */
    if (ec_decode_normal(&Finv, &S, faulty, surviving,
                         stripe, sector_bytes, gf) != 0)
        goto done;

    if (stats) {
        stats->u_Finv    = mat_nonzeros(&Finv);
        stats->u_S       = mat_nonzeros(&S);
        stats->mult_xors = gf_count_get();
    }
    rc = 0;

done:
    mat_free(&F); mat_free(&S); mat_free(&Finv); free(surviving);
    return rc;
}

int ec_syndrome_is_zero(const gf_mat *H, const uint8_t *stripe,
                        size_t sector_bytes, const gf_t *gf)
{
    uint8_t *acc = malloc(sector_bytes);
    int i, j, ok = 1;
    size_t b;

    if (!acc || sector_bytes == 0 || sector_bytes % (size_t)(gf->w / 8) != 0) {
        free(acc);
        return 0;
    }

    for (i = 0; i < H->rows && ok; i++) {
        memset(acc, 0, sector_bytes);
        for (j = 0; j < H->cols; j++) {
            uint32_t a = MAT(H, i, j);
            if (a == 0) continue;
            mult_XORs(stripe + (size_t)j * sector_bytes, acc, a,
                      sector_bytes, gf);
        }
        for (b = 0; b < sector_bytes; b++)
            if (acc[b] != 0) { ok = 0; break; }
    }

    free(acc);
    return ok;
}

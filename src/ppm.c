#include "ppm.h"

#include <omp.h>
#include <stdlib.h>
#include <string.h>

#include "codec.h"

typedef struct {
    gf_mat S;
    gf_mat Finv;
    int *faulty;
    int *surviving;
    int nf;
    int rc;
    uint64_t mult_xors;
} ppm_job_t;

static void job_free(ppm_job_t *job)
{
    mat_free(&job->S);
    mat_free(&job->Finv);
    free(job->faulty);
    free(job->surviving);
    memset(job, 0, sizeof *job);
}

static int copy_rows(const gf_mat *H, const int *rows, int nrows, gf_mat *out)
{
    int i;

    if (mat_alloc(out, nrows, H->cols) != 0) return -1;
    for (i = 0; i < nrows; i++)
        memcpy(out->e + (size_t)i * out->cols,
               H->e + (size_t)rows[i] * H->cols,
               sizeof *out->e * (size_t)H->cols);
    return 0;
}

static int prepare_job(const gf_mat *submatrix, const int *faulty, int nf,
                       const gf_t *gf, ppm_job_t *job)
{
    gf_mat F = { 0 };
    int ns;

    memset(job, 0, sizeof *job);
    job->faulty = malloc(sizeof *job->faulty * (size_t)nf);
    job->surviving = malloc(sizeof *job->surviving *
                            (size_t)(submatrix->cols - nf));
    if (!job->faulty || !job->surviving) goto fail;
    memcpy(job->faulty, faulty, sizeof *job->faulty * (size_t)nf);
    job->nf = nf;

    ns = ec_split(submatrix, faulty, nf, &F, &job->S, job->surviving);
    if (ns < 0 || F.rows != F.cols || mat_invert(&F, gf, &job->Finv) != 0)
        goto fail;
    mat_free(&F);
    return 0;

fail:
    mat_free(&F);
    job_free(job);
    return -1;
}

int ppm_recover_sd(const sd_code_t *code, const gf_mat *H,
                   const int *faulty, int nf,
                   uint8_t *stripe, size_t sector_bytes,
                   const gf_t *gf, int requested_threads,
                   ppm_stats_t *stats)
{
    char *is_faulty = NULL;
    int *row_faults = NULL, *row_is_independent = NULL;
    ppm_job_t *jobs = NULL, remainder = { 0 };
    int n, r, m, s, cols, p = 0, i, j, k, rc = -1;
    int effective_threads = 1, actual_threads = 1;
    uint64_t independent_count = 0, remainder_count = 0;

    if (stats) memset(stats, 0, sizeof *stats);
    if (!code || !H || !H->e || !faulty || !stripe || !gf ||
        requested_threads <= 0 || code->n <= code->m || code->r <= 0 ||
        code->m <= 0 || code->s <= 0 ||
        code->w != gf->w ||
        H->rows != sd_rows(code) || H->cols != sd_cols(code) ||
        nf != sd_rows(code) || H->cols <= nf || sector_bytes == 0 ||
        (gf->w != 8 && gf->w != 16 && gf->w != 32) ||
        sector_bytes % (size_t)(gf->w / 8) != 0)
        return -1;

    n = code->n;
    r = code->r;
    m = code->m;
    s = code->s;
    cols = H->cols;
    is_faulty = calloc((size_t)cols, 1);
    row_faults = calloc((size_t)r, sizeof *row_faults);
    row_is_independent = calloc((size_t)r, sizeof *row_is_independent);
    if (!is_faulty || !row_faults || !row_is_independent) goto done;

    for (i = 0; i < nf; i++) {
        if (faulty[i] < 0 || faulty[i] >= cols || is_faulty[faulty[i]])
            goto done;
        is_faulty[faulty[i]] = 1;
        row_faults[faulty[i] / n]++;
    }
    for (i = 0; i < r; i++)
        if (row_faults[i] == m) {
            row_is_independent[i] = 1;
            p++;
        }

    if (p > 0) {
        jobs = calloc((size_t)p, sizeof *jobs);
        if (!jobs) goto done;
    }

    k = 0;
    for (i = 0; i < r; i++) {
        gf_mat submatrix = { 0 };
        int *rows = NULL, *group_faulty = NULL;
        int f = 0;

        if (!row_is_independent[i]) continue;
        rows = malloc(sizeof *rows * (size_t)m);
        group_faulty = malloc(sizeof *group_faulty * (size_t)m);
        if (!rows || !group_faulty) {
            free(rows); free(group_faulty); goto done;
        }
        for (j = 0; j < m; j++) rows[j] = m * i + j;
        for (j = i * n; j < (i + 1) * n; j++)
            if (is_faulty[j]) group_faulty[f++] = j;
        if (f != m || copy_rows(H, rows, m, &submatrix) != 0 ||
            prepare_job(&submatrix, group_faulty, m, gf, &jobs[k]) != 0) {
            free(rows); free(group_faulty); mat_free(&submatrix); goto done;
        }
        free(rows);
        free(group_faulty);
        mat_free(&submatrix);
        k++;
    }

    {
        gf_mat Hrest = { 0 };
        int rest_rows = m * (r - p) + s;
        int dependent = nf - m * p;
        int *rows = malloc(sizeof *rows * (size_t)rest_rows);
        int *rest_faulty = malloc(sizeof *rest_faulty * (size_t)dependent);
        int nr = 0, nd = 0;

        if (!rows || !rest_faulty || dependent != rest_rows) {
            free(rows); free(rest_faulty); goto done;
        }
        for (i = 0; i < r; i++)
            if (!row_is_independent[i])
                for (j = 0; j < m; j++) rows[nr++] = m * i + j;
        for (i = 0; i < s; i++) rows[nr++] = m * r + i;
        for (i = 0; i < nf; i++)
            if (!row_is_independent[faulty[i] / n]) rest_faulty[nd++] = faulty[i];

        if (nr != rest_rows || nd != dependent ||
            copy_rows(H, rows, rest_rows, &Hrest) != 0 ||
            prepare_job(&Hrest, rest_faulty, dependent, gf, &remainder) != 0) {
            free(rows); free(rest_faulty); mat_free(&Hrest); goto done;
        }
        free(rows);
        free(rest_faulty);
        mat_free(&Hrest);
    }

    if (p > 0) {
        effective_threads = requested_threads < p ? requested_threads : p;
#pragma omp parallel num_threads(effective_threads)
        {
#pragma omp single
            actual_threads = omp_get_num_threads();
#pragma omp for schedule(static)
            for (i = 0; i < p; i++) {
                gf_count_reset();
                jobs[i].rc = ec_decode_matrix_first(
                    &jobs[i].Finv, &jobs[i].S,
                    jobs[i].faulty, jobs[i].surviving,
                    stripe, sector_bytes, gf);
                jobs[i].mult_xors = gf_count_get();
            }
        }
        for (i = 0; i < p; i++) {
            if (jobs[i].rc != 0) goto done;
            independent_count += jobs[i].mult_xors;
        }
    }

    gf_count_reset();
    if (ec_decode_normal(&remainder.Finv, &remainder.S,
                         remainder.faulty, remainder.surviving,
                         stripe, sector_bytes, gf) != 0)
        goto done;
    remainder_count = gf_count_get();

    if (stats) {
        stats->requested_threads = requested_threads;
        stats->effective_threads = p > 0 ? actual_threads : 1;
        stats->independent_groups = p;
        stats->dependent_failures = remainder.nf;
        stats->independent_mult_xors = independent_count;
        stats->remainder_mult_xors = remainder_count;
        stats->mult_xors = independent_count + remainder_count;
    }
    rc = 0;

done:
    if (jobs)
        for (i = 0; i < p; i++) job_free(&jobs[i]);
    free(jobs);
    job_free(&remainder);
    free(is_faulty);
    free(row_faults);
    free(row_is_independent);
    return rc;
}

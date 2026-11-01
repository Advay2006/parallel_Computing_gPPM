#include "gpu_ppm.h"

#include <stdlib.h>
#include <string.h>

#include "codec.h"
#include "gpu_backend.h"

#define GPU_SPLIT_PLANES 16U
#define GPU_SPLIT_SIZE 65536U

typedef struct {
    gf_mat S, Finv, G;
    int *faulty, *surviving;
    int nf;
    decode_sequence_t sequence;
    double cost;
    uint64_t operations;
} gpu_job_t;

static void job_free(gpu_job_t *job)
{
    mat_free(&job->S);
    mat_free(&job->Finv);
    mat_free(&job->G);
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
                       const gf_t *gf, gpu_job_t *job)
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

static int choose_job(gpu_job_t *job, const gf_t *gf, double c)
{
    if (ec_choose_sequence(&job->Finv, &job->S, gf, c, &job->sequence,
                           &job->cost) != 0)
        return -1;
    if (job->sequence == DECODE_MATRIX_FIRST) {
        if (mat_mul(&job->Finv, &job->S, gf, &job->G) != 0) return -1;
        job->operations = (uint64_t)mat_nonzeros(&job->G);
    } else {
        job->operations = (uint64_t)mat_nonzeros(&job->S) +
                          (uint64_t)mat_nonzeros(&job->Finv);
    }
    return 0;
}

static int decode_job(gpu_session_t *session, const gpu_job_t *job,
                      size_t sector_bytes, int workspace)
{
    if (job->sequence == DECODE_MATRIX_FIRST)
        return gpu_session_matvec(session, job->G.e, job->G.rows, job->G.cols,
                                  job->surviving, job->faulty, sector_bytes,
                                  0, 0, workspace);
    if (gpu_session_matvec(session, job->S.e, job->S.rows, job->S.cols,
                           job->surviving, NULL, sector_bytes,
                           0, 1, workspace) != 0)
        return -1;
    return gpu_session_matvec(session, job->Finv.e, job->Finv.rows,
                              job->Finv.cols, NULL, job->faulty,
                              sector_bytes, 1, 0, workspace);
}

int gpu_ppm_recover_sd(const sd_code_t *code, const gf_mat *H,
                       const int *faulty, int nf,
                       uint8_t *stripe, size_t sector_bytes,
                       const gf_t *gf, int kernel_variant,
                       gpu_ppm_stats_t *stats)
{
    char *is_faulty = NULL;
    int *row_faults = NULL, *row_is_independent = NULL;
    gpu_job_t *jobs = NULL, remainder = { 0 }, whole = { 0 };
    gpu_session_t *session = NULL;
    int n, r, m, s, cols, p = 0, i, j, k, rc = -1;
    size_t stripe_bytes, temp_bytes;
    double c, whole_cost = 0.0, ppm_cost = 0.0;
    uint64_t independent_operations = 0, remainder_operations = 0;
    int use_partitioning = 0;

    if (stats) memset(stats, 0, sizeof *stats);
    if (!code || !H || !H->e || !faulty || !stripe || !gf ||
        code->n <= code->m || code->r <= 0 || code->r > 24 ||
        code->m <= 0 || code->m > 3 ||
        code->s <= 0 || code->s > 3 || code->w != gf->w ||
        (gf->w != 8 && gf->w != 16 && gf->w != 32) ||
        H->rows != sd_rows(code) || H->cols != sd_cols(code) ||
        nf != sd_rows(code) || H->cols <= nf || sector_bytes == 0 ||
        sector_bytes % (size_t)(gf->w / 8) != 0 ||
        (kernel_variant != GPU_KERNEL_SCALAR &&
         kernel_variant != GPU_KERNEL_TABLE))
        return -1;

    n = code->n; r = code->r; m = code->m; s = code->s; cols = H->cols;
    if ((size_t)cols > SIZE_MAX / sector_bytes ||
        (size_t)H->rows > SIZE_MAX / sector_bytes)
        return -1;
    stripe_bytes = (size_t)cols * sector_bytes;
    temp_bytes = (size_t)H->rows * sector_bytes;
    c = gpu_calibrate_c(gf->w, kernel_variant, gf->logt,
                        gf->w == 32 ? 0 : (size_t)gf->order,
                        gf->expt,
                        gf->w == 32 ? 0 : 2 * (size_t)gf->nz + 1,
                        gf->split8,
                        gf->w == 32 ? (size_t)GPU_SPLIT_PLANES * GPU_SPLIT_SIZE : 0);
    if (prepare_job(H, faulty, nf, gf, &whole) != 0 ||
        choose_job(&whole, gf, c) != 0)
        goto done;
    whole_cost = whole.cost;

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
        gf_mat Hrow = { 0 };
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
        if (f != m || copy_rows(H, rows, m, &Hrow) != 0 ||
            prepare_job(&Hrow, group_faulty, m, gf, &jobs[k]) != 0 ||
            choose_job(&jobs[k], gf, c) != 0) {
            free(rows); free(group_faulty); mat_free(&Hrow); goto done;
        }
        ppm_cost += jobs[k].cost;
        free(rows); free(group_faulty); mat_free(&Hrow);
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
            prepare_job(&Hrest, rest_faulty, dependent, gf, &remainder) != 0 ||
            choose_job(&remainder, gf, c) != 0) {
            free(rows); free(rest_faulty); mat_free(&Hrest); goto done;
        }
        ppm_cost += remainder.cost;
        free(rows); free(rest_faulty); mat_free(&Hrest);
    }

    use_partitioning = p > 0 && ppm_cost < whole_cost;
    if (gpu_session_create(&session, stripe, stripe_bytes, temp_bytes, gf->w,
                           kernel_variant, gf->logt,
                           gf->w == 32 ? 0 : (size_t)gf->order,
                           gf->expt,
                           gf->w == 32 ? 0 : 2 * (size_t)gf->nz + 1,
                           gf->split8,
                           gf->w == 32 ? (size_t)GPU_SPLIT_PLANES * GPU_SPLIT_SIZE : 0) != 0)
        goto done;

    if (use_partitioning) {
        for (i = 0; i < p; i++) {
            if (decode_job(session, &jobs[i], sector_bytes, i) != 0) goto done;
            independent_operations += jobs[i].operations;
        }
        if (gpu_session_wait(session) != 0 ||
            decode_job(session, &remainder, sector_bytes, p) != 0)
            goto done;
        remainder_operations = remainder.operations;
    } else {
        if (decode_job(session, &whole, sector_bytes, 0) != 0) goto done;
        remainder_operations = whole.operations;
    }
    if (gpu_session_copy_back(session, stripe) != 0) goto done;

    if (stats) {
        stats->kernel_variant = kernel_variant;
        stats->used_partitioning = use_partitioning;
        stats->independent_groups = use_partitioning ? p : 0;
        stats->dependent_failures = use_partitioning ? remainder.nf : nf;
        stats->c_multiply = c;
        stats->estimated_cost = use_partitioning ? ppm_cost : whole.cost;
        stats->kernel_seconds_sum = gpu_session_kernel_seconds_sum(session);
        stats->mult_xors = independent_operations + remainder_operations;
    }
    rc = 0;

done:
    gpu_session_destroy(session);
    if (jobs)
        for (i = 0; i < p; i++) job_free(&jobs[i]);
    free(jobs);
    job_free(&remainder); job_free(&whole);
    free(is_faulty); free(row_faults); free(row_is_independent);
    return rc;
}

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "coefficients.h"
#include "codec.h"
#include "gpu_backend.h"
#include "gpu_ppm.h"
#include "ppm.h"

#define BENCH_N 16
#define BENCH_R 16
#define BENCH_Z 1
#define STRIPE_BYTES (32U * 1024U * 1024U)
#define DEFAULT_TRIALS 10
#define DEFAULT_CSV "results/milestone_gpu/gpu_benchmark.csv"

typedef struct {
    double seconds;
    uint64_t checksum;
    uint64_t operations;
    double c;
    double estimated_cost;
    double kernel_seconds_sum;
    int used_partitioning;
} trial_result_t;

static uint32_t rng = 0x243f6a88U;

static uint8_t random_byte(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (uint8_t)rng;
}

static double now_seconds(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static uint64_t checksum64(const uint8_t *data, size_t bytes)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t i;
    for (i = 0; i < bytes; i++) {
        hash ^= data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int compare_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *values, int n, double *scratch)
{
    int i;
    for (i = 0; i < n; i++) scratch[i] = values[i];
    qsort(scratch, (size_t)n, sizeof *scratch, compare_double);
    return n & 1 ? scratch[n / 2]
                 : (scratch[n / 2 - 1] + scratch[n / 2]) / 2.0;
}

static int parse_trials(const char *text, int *trials)
{
    char *end;
    long value;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno || end == text || *end || value < 1 || value > 100) return -1;
    *trials = (int)value;
    return 0;
}

static void fill_data(uint8_t *stripe, size_t sectors, size_t sector_bytes,
                      const uint8_t *is_parity)
{
    size_t i, b;
    for (i = 0; i < sectors; i++)
        if (!is_parity[i])
            for (b = 0; b < sector_bytes; b++)
                stripe[i * sector_bytes + b] = random_byte();
}

static void inject_faults(uint8_t *stripe, size_t sector_bytes,
                          const int *faulty, int nf)
{
    int i;
    for (i = 0; i < nf; i++)
        memset(stripe + (size_t)faulty[i] * sector_bytes, 0xa5, sector_bytes);
}

static int recover_once(int implementation, const sd_code_t *code,
                        const gf_mat *H, const int *faulty, int nf,
                        uint8_t *stripe, size_t sector_bytes, const gf_t *gf,
                        trial_result_t *result)
{
    ppm_stats_t cpu_stats = { 0 };
    gpu_ppm_stats_t gpu_stats = { 0 };
    double start = now_seconds(), end;
    int rc;

    if (start < 0.0) return -1;
    if (implementation == 0)
        rc = ppm_recover_sd(code, H, faulty, nf, stripe, sector_bytes, gf,
                            8, &cpu_stats);
    else
        rc = gpu_ppm_recover_sd(code, H, faulty, nf, stripe, sector_bytes,
                                gf, implementation == 1 ? GPU_KERNEL_SCALAR
                                                        : GPU_KERNEL_TABLE,
                                &gpu_stats);
    end = now_seconds();
    if (rc != 0 || end <= start) return -1;
    result->seconds = end - start;
    if (implementation == 0) {
        result->operations = cpu_stats.mult_xors;
        result->c = cpu_stats.c_multiply;
        result->estimated_cost = cpu_stats.estimated_cost;
        result->kernel_seconds_sum = 0.0;
        result->used_partitioning = cpu_stats.used_partitioning;
    } else {
        result->operations = gpu_stats.mult_xors;
        result->c = gpu_stats.c_multiply;
        result->estimated_cost = gpu_stats.estimated_cost;
        result->kernel_seconds_sum = gpu_stats.kernel_seconds_sum;
        result->used_partitioning = gpu_stats.used_partitioning;
    }
    return 0;
}

static int run_variant(FILE *csv, const char *label, int implementation,
                       const sd_code_t *code, const gf_mat *H,
                       const int *faulty, int nf, uint8_t *stripe,
                       const uint8_t *golden, size_t stripe_bytes,
                       size_t data_bytes, size_t sector_bytes, const gf_t *gf,
                       int trials, double *median_seconds)
{
    double *times = malloc(sizeof *times * (size_t)trials);
    double *scratch = malloc(sizeof *scratch * (size_t)trials);
    uint64_t expected_checksum = checksum64(golden, stripe_bytes);
    uint64_t expected_operations = 0;
    int trial, ok = 0;

    if (!times || !scratch) goto done;
    for (trial = -1; trial < trials; trial++) {
        trial_result_t result = { 0 };
        memcpy(stripe, golden, stripe_bytes);
        inject_faults(stripe, sector_bytes, faulty, nf);
        if (recover_once(implementation, code, H, faulty, nf, stripe,
                         sector_bytes, gf, &result) != 0 ||
            memcmp(stripe, golden, stripe_bytes) != 0 ||
            (result.checksum = checksum64(stripe, stripe_bytes)) != expected_checksum ||
            !ec_syndrome_is_zero(H, stripe, sector_bytes, gf)) {
            fprintf(stderr, "%s recovery validation failed for m=%d s=%d trial=%d\n",
                    label, code->m, code->s, trial + 1);
            goto done;
        }
        if (trial >= 0) {
            if (result.operations != expected_operations) {
                fprintf(stderr, "%s operation count changed between trials\n", label);
                goto done;
            }
            times[trial] = result.seconds;
            if (fprintf(csv,
                        "%s,%d,%d,%d,%d,%d,%d,%d,%.9f,%.3f,%016" PRIx64
                        ",%d,%.6f,%.6f,%.9f,%" PRIu64 ",PASS\n",
                        label, code->n, code->m, code->s, code->r, code->w,
                        BENCH_Z, trial + 1, result.seconds,
                        ((double)data_bytes / (1024.0 * 1024.0)) / result.seconds,
                        result.checksum, result.used_partitioning, result.c,
                        result.estimated_cost, result.kernel_seconds_sum,
                        result.operations) < 0)
                goto done;
        } else expected_operations = result.operations;
    }
    *median_seconds = median(times, trials, scratch);
    ok = 1;

done:
    free(times); free(scratch);
    return ok ? 0 : -1;
}

static int benchmark_config(FILE *csv, const sd_coeff_entry *entry,
                            const gf_t *gf, int trials)
{
    sd_code_t code = {
        entry->n, entry->r, entry->m, entry->s, entry->w, entry->a
    };
    gf_mat H = { 0 };
    size_t sectors = (size_t)sd_cols(&code);
    size_t sector_bytes = STRIPE_BYTES / sectors;
    size_t data_bytes = (sectors - (size_t)sd_rows(&code)) * sector_bytes;
    uint8_t *golden = NULL, *stripe = NULL, *is_parity = NULL;
    double cpu_median, gpu_scalar_median, gpu_optimized_median;
    int parity[80], faulty[80], np, nf, i, rc = -1;
    decode_stats_t encode_stats = { 0 };

    if (STRIPE_BYTES % sectors != 0 ||
        sector_bytes % (size_t)(code.w / 8) != 0 ||
        sd_build_H(&code, gf, &H) != 0)
        return -1;
    golden = calloc(1, STRIPE_BYTES);
    stripe = malloc(STRIPE_BYTES);
    is_parity = calloc(sectors, 1);
    if (!golden || !stripe || !is_parity) goto done;
    np = sd_parity_sectors(&code, parity);
    nf = sd_failure_sectors(&code, BENCH_Z, faulty);
    if (np != sd_rows(&code) || nf != sd_rows(&code)) goto done;
    for (i = 0; i < np; i++) is_parity[parity[i]] = 1;
    fill_data(golden, sectors, sector_bytes, is_parity);
    if (ec_encode(&H, parity, np, golden, sector_bytes, gf, &encode_stats) != 0 ||
        !ec_syndrome_is_zero(&H, golden, sector_bytes, gf))
        goto done;

    if (run_variant(csv, "cpu_optimized_m3", 0, &code, &H, faulty, nf,
                    stripe, golden, STRIPE_BYTES, data_bytes, sector_bytes,
                    gf, trials, &cpu_median) != 0 ||
        run_variant(csv, "gpu_scalar", 1, &code, &H, faulty, nf,
                    stripe, golden, STRIPE_BYTES, data_bytes, sector_bytes,
                    gf, trials, &gpu_scalar_median) != 0 ||
        run_variant(csv, "gpu_optimized", 2, &code, &H, faulty, nf,
                    stripe, golden, STRIPE_BYTES, data_bytes, sector_bytes,
                    gf, trials, &gpu_optimized_median) != 0)
        goto done;

    printf("m=%d s=%d w=%d CPU-M3 %.3f ms, GPU scalar %.2fx, GPU optimized %.2fx vs CPU\n",
           code.m, code.s, code.w, cpu_median * 1000.0,
           cpu_median / gpu_scalar_median, cpu_median / gpu_optimized_median);
    rc = 0;

done:
    free(golden); free(stripe); free(is_parity);
    mat_free(&H);
    return rc;
}

int main(int argc, char **argv)
{
    const char *coeff_path = argc > 1 ? argv[1] : "data/FAST-Coefficients.txt";
    const char *csv_path = argc > 2 ? argv[2] : DEFAULT_CSV;
    int trials = DEFAULT_TRIALS;
    sd_coeff_table table = { 0 };
    gf_t gf8 = { 0 }, gf16 = { 0 }, gf32 = { 0 };
    FILE *csv = NULL;
    int m, s, status = 2;

    if (argc > 4 || (argc > 3 && parse_trials(argv[3], &trials) != 0)) {
        fprintf(stderr, "usage: %s [coefficients] [csv-output] [trials]\n", argv[0]);
        return 2;
    }
    if (mkdir("results", 0777) != 0 && errno != EEXIST) goto done;
    if (mkdir("results/milestone_gpu", 0777) != 0 && errno != EEXIST) goto done;
    if (sd_coeff_load(coeff_path, &table) != 0 ||
        gf_init(&gf8, 8) != 0 || gf_init(&gf16, 16) != 0 ||
        gf_init(&gf32, 32) != 0)
        goto done;
    csv = fopen(csv_path, "w");
    if (!csv) goto done;
    if (fprintf(csv,
                "implementation,n,m,s,r,w,z,trial,seconds,data_MiB_s,checksum,"
                "used_partitioning,c_multiply,estimated_cost,kernel_seconds_sum,logical_mult_xors,status\n") < 0)
        goto done;

    printf("GPU scalar/optimized vs CPU M3: 32 MiB stripes, %d trials\n", trials);
    for (m = 1; m <= 3; m++)
        for (s = 1; s <= 3; s++) {
            const sd_coeff_entry *entry =
                sd_coeff_find(&table, BENCH_N, m, s, BENCH_R);
            const gf_t *gf;
            if (!entry) goto done;
            gf = entry->w == 8 ? &gf8 : entry->w == 16 ? &gf16 : &gf32;
            if (benchmark_config(csv, entry, gf, trials) != 0) goto done;
        }
    if (ferror(csv) || fclose(csv) != 0) {
        csv = NULL;
        goto done;
    }
    csv = NULL;
    printf("All %d GPU/CPU variants passed validation.\n", 9 * 3 * trials);
    printf("Raw results: %s\n", csv_path);
    status = 0;

done:
    if (csv) fclose(csv);
    gf_free(&gf8); gf_free(&gf16); gf_free(&gf32);
    sd_coeff_free(&table);
    return status;
}

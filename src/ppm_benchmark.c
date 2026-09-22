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
#include "ppm.h"

#define BENCH_N 16
#define BENCH_R 16
#define BENCH_Z 1
#define STRIPE_BYTES (32U * 1024U * 1024U)
#define DEFAULT_TRIALS 10
#define DEFAULT_CSV "results/ppm_benchmark_results.csv"

typedef struct {
    double seconds;
    int requested_threads;
    int effective_threads;
    int independent_groups;
    int dependent_failures;
    uint64_t independent_mult_xors;
    uint64_t remainder_mult_xors;
    uint64_t mult_xors;
    uint64_t checksum;
} trial_result_t;

static uint32_t rng = 0x243f6a88U;

static uint8_t rnd_byte(void)
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

static int compare_double(const void *left, const void *right)
{
    double a = *(const double *)left, b = *(const double *)right;

    return (a > b) - (a < b);
}

static int parse_trials(const char *text, int *trials)
{
    char *end;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 1 || value > 100)
        return -1;
    *trials = (int)value;
    return 0;
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

static void inject_faults(uint8_t *stripe, size_t sector_bytes,
                          const int *faulty, int nf)
{
    int i;

    for (i = 0; i < nf; i++)
        memset(stripe + (size_t)faulty[i] * sector_bytes, 0xa5, sector_bytes);
}

static int validate_stripe(const gf_mat *H, const uint8_t *stripe,
                           const uint8_t *golden, size_t stripe_bytes,
                           size_t sector_bytes, const gf_t *gf,
                           uint64_t golden_checksum, uint64_t *checksum)
{
    *checksum = checksum64(stripe, stripe_bytes);
    return memcmp(stripe, golden, stripe_bytes) == 0 &&
           *checksum == golden_checksum &&
           ec_syndrome_is_zero(H, stripe, sector_bytes, gf);
}

static int recover_once(int use_ppm, const sd_code_t *code, const gf_mat *H,
                        const int *faulty, int nf, uint8_t *stripe,
                        size_t sector_bytes, const gf_t *gf,
                        int requested_threads, trial_result_t *result)
{
    decode_stats_t baseline_stats = { 0 };
    ppm_stats_t ppm_stats = { 0 };
    double start, end;
    int rc;

    start = now_seconds();
    if (start < 0.0) return -1;
    if (use_ppm)
        rc = ppm_recover_sd(code, H, faulty, nf, stripe, sector_bytes, gf,
                            requested_threads, &ppm_stats);
    else
        rc = ec_recover(H, faulty, nf, stripe, sector_bytes, gf,
                        &baseline_stats);
    end = now_seconds();
    if (end < 0.0 || end <= start) return -1;

    memset(result, 0, sizeof *result);
    result->seconds = end - start;
    if (use_ppm) {
        result->requested_threads = ppm_stats.requested_threads;
        result->effective_threads = ppm_stats.effective_threads;
        result->independent_groups = ppm_stats.independent_groups;
        result->dependent_failures = ppm_stats.dependent_failures;
        result->independent_mult_xors = ppm_stats.independent_mult_xors;
        result->remainder_mult_xors = ppm_stats.remainder_mult_xors;
        result->mult_xors = ppm_stats.mult_xors;
    } else {
        result->requested_threads = 1;
        result->effective_threads = 1;
        result->dependent_failures = nf;
        result->remainder_mult_xors = baseline_stats.mult_xors;
        result->mult_xors = baseline_stats.mult_xors;
    }
    return rc;
}

static int result_counts_are_valid(const trial_result_t *result, int use_ppm,
                                   const sd_code_t *code, int requested_threads,
                                   uint64_t expected_count,
                                   uint64_t baseline_count)
{
    if (result->mult_xors != result->independent_mult_xors +
                             result->remainder_mult_xors ||
        result->mult_xors != expected_count)
        return 0;
    if (!use_ppm)
        return result->requested_threads == 1 &&
               result->effective_threads == 1 &&
               result->independent_groups == 0;
    return result->requested_threads == requested_threads &&
           result->effective_threads >= 1 &&
           result->effective_threads <= requested_threads &&
           result->independent_groups == code->r - BENCH_Z &&
           result->dependent_failures == code->m * BENCH_Z + code->s &&
           result->mult_xors <= baseline_count;
}

static int run_warmup(int use_ppm, const sd_code_t *code, const gf_mat *H,
                      const int *faulty, int nf, uint8_t *stripe,
                      const uint8_t *golden, size_t stripe_bytes,
                      size_t sector_bytes, const gf_t *gf,
                      int requested_threads, uint64_t golden_checksum,
                      uint64_t baseline_count, uint64_t *measured_count)
{
    trial_result_t result;

    inject_faults(stripe, sector_bytes, faulty, nf);
    if (recover_once(use_ppm, code, H, faulty, nf, stripe, sector_bytes, gf,
                     requested_threads, &result) != 0 ||
        !validate_stripe(H, stripe, golden, stripe_bytes, sector_bytes, gf,
                         golden_checksum, &result.checksum))
        return -1;
    *measured_count = result.mult_xors;
    return result_counts_are_valid(&result, use_ppm, code, requested_threads,
                                   *measured_count,
                                   use_ppm ? baseline_count : *measured_count)
           ? 0 : -1;
}

static int run_trials(int use_ppm, const sd_code_t *code, const gf_mat *H,
                      const int *faulty, int nf, uint8_t *stripe,
                      const uint8_t *golden, size_t stripe_bytes,
                      size_t sector_bytes, const gf_t *gf,
                      int requested_threads, int trials,
                      uint64_t golden_checksum, uint64_t baseline_count,
                      uint64_t expected_count, trial_result_t *results)
{
    int trial;

    for (trial = 0; trial < trials; trial++) {
        trial_result_t *result = &results[trial];

        inject_faults(stripe, sector_bytes, faulty, nf);
        if (recover_once(use_ppm, code, H, faulty, nf, stripe, sector_bytes, gf,
                         requested_threads, result) != 0 ||
            !validate_stripe(H, stripe, golden, stripe_bytes, sector_bytes, gf,
                             golden_checksum, &result->checksum) ||
            !result_counts_are_valid(result, use_ppm, code, requested_threads,
                                     expected_count, baseline_count)) {
            fprintf(stderr, "%s validation failed for m=%d s=%d threads=%d trial=%d\n",
                    use_ppm ? "PPM" : "baseline", code->m, code->s,
                    requested_threads, trial + 1);
            return -1;
        }
    }
    return 0;
}

static double median_time(const trial_result_t *results, int trials,
                          double *scratch)
{
    int i;

    for (i = 0; i < trials; i++) scratch[i] = results[i].seconds;
    qsort(scratch, (size_t)trials, sizeof *scratch, compare_double);
    return trials % 2 ? scratch[trials / 2]
                      : (scratch[trials / 2 - 1] + scratch[trials / 2]) / 2.0;
}

static int write_results(FILE *csv, const char *implementation,
                         const sd_code_t *code, size_t stripe_bytes,
                         size_t data_bytes, size_t sector_bytes,
                         const trial_result_t *results, int trials,
                         uint64_t baseline_count, double baseline_median)
{
    double data_mib = (double)data_bytes / (1024.0 * 1024.0);
    double codeword_mib = (double)stripe_bytes / (1024.0 * 1024.0);
    int trial;

    for (trial = 0; trial < trials; trial++) {
        const trial_result_t *result = &results[trial];
        double speedup = baseline_median / result->seconds;
        double efficiency = speedup / result->effective_threads;

        if (fprintf(csv,
                    "%s,%d,%d,%d,%d,%d,%d,%zu,%zu,%zu,%d,%d,%d,%d,%d,%d,"
                    "%.9f,%.3f,%.3f,%" PRIu64 ",%" PRIu64 ",%" PRIu64
                    ",%" PRIu64 ",%.9f,%.6f,%.6f,%016" PRIx64 ",PASS\n",
                    implementation, code->n, code->m, code->s, code->r,
                    code->w, BENCH_Z, stripe_bytes, data_bytes, sector_bytes,
                    trial + 1, trials, result->requested_threads,
                    result->effective_threads, result->independent_groups,
                    result->dependent_failures, result->seconds,
                    data_mib / result->seconds,
                    codeword_mib / result->seconds,
                    result->independent_mult_xors,
                    result->remainder_mult_xors, result->mult_xors,
                    baseline_count, baseline_median, speedup, efficiency,
                    result->checksum) < 0)
            return -1;
    }
    return 0;
}

static int benchmark_config(FILE *csv, const sd_coeff_entry *entry,
                            const gf_t *gf, int trials)
{
    static const int thread_counts[] = { 1, 2, 4, 8 };
    sd_code_t code = {
        entry->n, entry->r, entry->m, entry->s, entry->w, entry->a
    };
    gf_mat H = { 0 };
    size_t sectors = (size_t)sd_cols(&code);
    size_t sector_bytes = STRIPE_BYTES / sectors;
    size_t data_bytes = (sectors - (size_t)sd_rows(&code)) * sector_bytes;
    uint8_t *stripe = NULL, *golden = NULL, *is_parity = NULL;
    trial_result_t *results = NULL;
    double *scratch = NULL, baseline_median;
    uint64_t golden_checksum, baseline_count, ppm_count;
    int parity[80], faulty[80], np, nf, i, t, rc = -1;
    decode_stats_t encode_stats;

    if (STRIPE_BYTES % sectors != 0 ||
        sector_bytes % (size_t)(code.w / 8) != 0 ||
        sd_build_H(&code, gf, &H) != 0)
        return -1;
    stripe = calloc(1, STRIPE_BYTES);
    golden = calloc(1, STRIPE_BYTES);
    is_parity = calloc(sectors, 1);
    results = calloc((size_t)trials, sizeof *results);
    scratch = malloc((size_t)trials * sizeof *scratch);
    if (!stripe || !golden || !is_parity || !results || !scratch) goto done;

    np = sd_parity_sectors(&code, parity);
    nf = sd_failure_sectors(&code, BENCH_Z, faulty);
    if (np != sd_rows(&code) || nf != sd_rows(&code)) goto done;
    for (i = 0; i < np; i++) is_parity[parity[i]] = 1;
    for (i = 0; i < (int)sectors; i++)
        if (!is_parity[i]) {
            size_t b;

            for (b = 0; b < sector_bytes; b++)
                golden[(size_t)i * sector_bytes + b] = rnd_byte();
        }
    if (ec_encode(&H, parity, np, golden, sector_bytes, gf, &encode_stats) != 0 ||
        !ec_syndrome_is_zero(&H, golden, sector_bytes, gf))
        goto done;
    memcpy(stripe, golden, STRIPE_BYTES);
    golden_checksum = checksum64(golden, STRIPE_BYTES);

    if (run_warmup(0, &code, &H, faulty, nf, stripe, golden, STRIPE_BYTES,
                   sector_bytes, gf, 1, golden_checksum, 0,
                   &baseline_count) != 0 ||
        run_trials(0, &code, &H, faulty, nf, stripe, golden, STRIPE_BYTES,
                   sector_bytes, gf, 1, trials, golden_checksum,
                   baseline_count, baseline_count, results) != 0)
        goto done;
    baseline_median = median_time(results, trials, scratch);
    if (write_results(csv, "baseline", &code, STRIPE_BYTES, data_bytes,
                      sector_bytes, results, trials, baseline_count,
                      baseline_median) != 0)
        goto done;

    printf("  m=%d s=%d w=%d baseline median %.3f s",
           code.m, code.s, code.w, baseline_median);
    for (t = 0; t < (int)(sizeof thread_counts / sizeof thread_counts[0]); t++) {
        int requested = thread_counts[t];
        double ppm_median;

        if (run_warmup(1, &code, &H, faulty, nf, stripe, golden, STRIPE_BYTES,
                       sector_bytes, gf, requested, golden_checksum,
                       baseline_count, &ppm_count) != 0 ||
            run_trials(1, &code, &H, faulty, nf, stripe, golden, STRIPE_BYTES,
                       sector_bytes, gf, requested, trials, golden_checksum,
                       baseline_count, ppm_count, results) != 0)
            goto done;
        ppm_median = median_time(results, trials, scratch);
        if (write_results(csv, "ppm", &code, STRIPE_BYTES, data_bytes,
                          sector_bytes, results, trials, baseline_count,
                          baseline_median) != 0)
            goto done;
        printf("  PPM-%d %.2fx", requested, baseline_median / ppm_median);
    }
    putchar('\n');
    rc = 0;

done:
    free(stripe); free(golden); free(is_parity); free(results); free(scratch);
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
    if (strcmp(csv_path, DEFAULT_CSV) == 0 &&
        mkdir("results", 0777) != 0 && errno != EEXIST) {
        fprintf(stderr, "cannot create results directory\n");
        goto done;
    }
    if (sd_coeff_load(coeff_path, &table) != 0) goto done;
    if (gf_init(&gf8, 8) != 0 || gf_init(&gf16, 16) != 0 ||
        gf_init(&gf32, 32) != 0) {
        fprintf(stderr, "cannot initialize Galois fields\n");
        goto done;
    }
    csv = fopen(csv_path, "w");
    if (!csv) {
        fprintf(stderr, "cannot write %s\n", csv_path);
        goto done;
    }
    if (fprintf(csv,
                "implementation,n,m,s,r,w,z,stripe_bytes,data_bytes,sector_bytes,"
                "trial,trials,requested_threads,effective_threads,independent_groups,"
                "dependent_failures,seconds,data_MiB_s,codeword_MiB_s,"
                "independent_mult_xors,remainder_mult_xors,measured_mult_xors,"
                "baseline_mult_xors,baseline_median_seconds,speedup,efficiency,"
                "checksum,status\n") < 0)
        goto done;

    printf("Baseline and PPM decode: 32 MiB codeword, n=16, r=16, z=1, %d trials\n",
           trials);
    for (m = 1; m <= 3; m++)
        for (s = 1; s <= 3; s++) {
            const sd_coeff_entry *entry =
                sd_coeff_find(&table, BENCH_N, m, s, BENCH_R);
            const gf_t *gf;

            if (!entry) {
                fprintf(stderr, "missing benchmark coefficients for m=%d s=%d\n",
                        m, s);
                goto done;
            }
            gf = entry->w == 8 ? &gf8 : entry->w == 16 ? &gf16 : &gf32;
            if (benchmark_config(csv, entry, gf, trials) != 0) goto done;
        }
    if (ferror(csv) || fclose(csv) != 0) {
        csv = NULL;
        fprintf(stderr, "error writing %s\n", csv_path);
        goto done;
    }
    csv = NULL;
    printf("All %d measured decode trials passed validation.\n", 9 * 5 * trials);
    printf("Raw results: %s\n", csv_path);
    status = 0;

done:
    if (csv) fclose(csv);
    gf_free(&gf8); gf_free(&gf16); gf_free(&gf32);
    sd_coeff_free(&table);
    return status;
}

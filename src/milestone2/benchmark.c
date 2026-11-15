#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "coefficients.h"
#include "codec.h"
#include "gf.h"
#include "matrix.h"
#include "sd_code.h"

#define BENCH_N 16
#define BENCH_R 16
#define STRIPE_BYTES (32U * 1024U * 1024U)
#define DEFAULT_TRIALS 10

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

static long C1_formula(const sd_code_t *code, int z)
{
    return (long)code->n * code->r * (code->m + code->s)
         + (long)code->m * (code->m * code->r + code->s) * (z - 1)
         + (long)code->m * code->m * (code->r - z);
}

static int benchmark_operation(FILE *csv, const char *operation,
                               const sd_code_t *code, const gf_mat *H,
                               const int *unknown, int nunknown,
                               uint8_t *stripe, const uint8_t *golden,
                               size_t sector_bytes, size_t stripe_bytes,
                               size_t data_bytes, const gf_t *gf, int trials)
{
    double *times, sum = 0.0, median, minimum;
    const double data_mib = (double)data_bytes / (1024.0 * 1024.0);
    const double codeword_mib = (double)stripe_bytes / (1024.0 * 1024.0);
    const long c1 = C1_formula(code, 1);
    decode_stats_t stats;
    int i, trial, ok = 1;

    times = calloc((size_t)trials, sizeof *times);
    if (!times) return -1;

    for (i = 0; i < nunknown; i++)
        memset(stripe + (size_t)unknown[i] * sector_bytes, 0xa5, sector_bytes);
    if (ec_recover(H, unknown, nunknown, stripe, sector_bytes, gf, &stats) != 0 ||
        memcmp(stripe, golden, stripe_bytes) != 0 ||
        !ec_syndrome_is_zero(H, stripe, sector_bytes, gf)) {
        fprintf(stderr, "%s warm-up failed for m=%d s=%d w=%d\n",
                operation, code->m, code->s, code->w);
        free(times);
        return -1;
    }

    for (trial = 0; trial < trials; trial++) {
        double start, end, elapsed;
        long delta;
        const char *count_status;
        uint64_t checksum;

        for (i = 0; i < nunknown; i++)
            memset(stripe + (size_t)unknown[i] * sector_bytes, 0xa5, sector_bytes);

        start = now_seconds();
        if (start < 0 || ec_recover(H, unknown, nunknown, stripe,
                                    sector_bytes, gf, &stats) != 0) {
            free(times);
            return -1;
        }
        end = now_seconds();
        elapsed = end - start;
        if (elapsed <= 0.0) {
            free(times);
            return -1;
        }

        ok = memcmp(stripe, golden, stripe_bytes) == 0
          && ec_syndrome_is_zero(H, stripe, sector_bytes, gf)
          && stats.mult_xors == (uint64_t)(stats.u_S + stats.u_Finv);
        delta = (long)stats.mult_xors - c1;
        count_status = delta == 0 ? "MATCH" : delta < 0 ? "UNDER" : "OVER";
        if (delta > 0) ok = 0;
        checksum = checksum64(stripe, stripe_bytes);
        times[trial] = elapsed;
        sum += elapsed;

        if (fprintf(csv,
                    "%s,%d,%d,%d,%d,%d,1,%zu,%zu,%zu,%d,%.9f,%llu,%ld,%ld,%s,%.3f,%.3f,%016llx,%s\n",
                    operation, code->n, code->m, code->s, code->r, code->w,
                    stripe_bytes, data_bytes, sector_bytes, trial + 1, elapsed,
                    (unsigned long long)stats.mult_xors, c1, delta, count_status,
                    data_mib / elapsed, codeword_mib / elapsed,
                    (unsigned long long)checksum, ok ? "PASS" : "FAIL") < 0) {
            free(times);
            return -1;
        }
        if (!ok) {
            fprintf(stderr, "%s validation failed for m=%d s=%d w=%d trial=%d\n",
                    operation, code->m, code->s, code->w, trial + 1);
            free(times);
            return -1;
        }
    }

    qsort(times, (size_t)trials, sizeof *times, compare_double);
    minimum = times[0];
    median = trials % 2 ? times[trials / 2]
                        : (times[trials / 2 - 1] + times[trials / 2]) / 2.0;
    printf("  %-6s m=%d s=%d w=%2d  mean %8.2f  median %8.2f  best %8.2f data MiB/s\n",
           operation, code->m, code->s, code->w,
           data_mib / (sum / trials), data_mib / median, data_mib / minimum);

    free(times);
    return 0;
}

static int benchmark_config(FILE *csv, const sd_coeff_entry *entry,
                            const gf_t *gf, int trials)
{
    sd_code_t code = {
        entry->n, entry->r, entry->m, entry->s, entry->w, entry->a
    };
    gf_mat H = { 0 };
    const size_t sectors = (size_t)code.n * code.r;
    const size_t sector_bytes = STRIPE_BYTES / sectors;
    const size_t data_bytes = (sectors - (size_t)sd_rows(&code)) * sector_bytes;
    uint8_t *stripe = NULL, *golden = NULL, *is_parity = NULL;
    int parity[80], faulty[80], np, nf, i, rc = -1;
    decode_stats_t stats;

    if (STRIPE_BYTES % sectors != 0 ||
        sector_bytes % (size_t)(code.w / 8) != 0 ||
        sd_build_H(&code, gf, &H) != 0)
        return -1;

    stripe = calloc(1, STRIPE_BYTES);
    golden = calloc(1, STRIPE_BYTES);
    is_parity = calloc(sectors, 1);
    if (!stripe || !golden || !is_parity) goto done;

    np = sd_parity_sectors(&code, parity);
    nf = sd_failure_sectors(&code, 1, faulty);
    if (np != sd_rows(&code) || nf != sd_rows(&code)) goto done;
    for (i = 0; i < np; i++) is_parity[parity[i]] = 1;
    for (i = 0; i < (int)sectors; i++)
        if (!is_parity[i]) {
            size_t b;
            for (b = 0; b < sector_bytes; b++)
                stripe[(size_t)i * sector_bytes + b] = rnd_byte();
        }

    if (ec_encode(&H, parity, np, stripe, sector_bytes, gf, &stats) != 0 ||
        !ec_syndrome_is_zero(&H, stripe, sector_bytes, gf))
        goto done;
    memcpy(golden, stripe, STRIPE_BYTES);

    if (benchmark_operation(csv, "encode", &code, &H, parity, np,
                            stripe, golden, sector_bytes, STRIPE_BYTES,
                            data_bytes, gf, trials) != 0)
        goto done;
    if (benchmark_operation(csv, "decode", &code, &H, faulty, nf,
                            stripe, golden, sector_bytes, STRIPE_BYTES,
                            data_bytes, gf, trials) != 0)
        goto done;
    rc = 0;

done:
    free(stripe); free(golden); free(is_parity);
    mat_free(&H);
    return rc;
}

int main(int argc, char **argv)
{
    const char *coeff_path = argc > 1 ? argv[1] : "data/FAST-Coefficients.txt";
    const char *csv_path = argc > 2 ? argv[2] : "results/benchmark_results.csv";
    int trials = DEFAULT_TRIALS;
    sd_coeff_table table = { 0 };
    gf_t gf8 = { 0 }, gf16 = { 0 }, gf32 = { 0 };
    FILE *csv = NULL;
    int m, s, status = 2;

    if (argc > 4 || (argc > 3 && parse_trials(argv[3], &trials) != 0)) {
        fprintf(stderr, "usage: %s [coefficients] [csv-output] [trials]\n", argv[0]);
        return 2;
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
    fprintf(csv, "operation,n,m,s,r,w,z,stripe_bytes,data_bytes,sector_bytes,"
                 "trial,seconds,measured_C,C1,delta,count_status,data_MiB_s,"
                 "codeword_MiB_s,checksum,status\n");

    printf("Sequential SD baseline: 32 MiB codeword, n=16, r=16, %d trials\n",
           trials);
    printf("Throughput below uses non-parity (useful-data) bytes.\n\n");
    for (m = 1; m <= 3; m++)
        for (s = 1; s <= 3; s++) {
            const sd_coeff_entry *entry = sd_coeff_find(&table, BENCH_N, m, s, BENCH_R);
            const gf_t *gf;
            if (!entry) {
                fprintf(stderr, "missing benchmark coefficients for m=%d s=%d\n", m, s);
                goto done;
            }
            gf = entry->w == 8 ? &gf8 : entry->w == 16 ? &gf16 : &gf32;
            if (benchmark_config(csv, entry, gf, trials) != 0) goto done;
        }

    {
        int csv_error = ferror(csv);
        if (fclose(csv) != 0) csv_error = 1;
        csv = NULL;
        if (csv_error) {
            fprintf(stderr, "error writing %s\n", csv_path);
            goto done;
        }
    }
    printf("\nAll %d benchmark trials passed validation.\n", 9 * 2 * trials);
    printf("Raw results: %s\n", csv_path);
    status = 0;

done:
    if (csv) fclose(csv);
    gf_free(&gf8); gf_free(&gf16); gf_free(&gf32);
    sd_coeff_free(&table);
    return status;
}

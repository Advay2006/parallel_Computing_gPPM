#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gf.h"

#define REGION_BYTES (128U * 1024U)
#define REGION_CALLS 256
#define TRIALS 10

static double now_seconds(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int compare_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *values)
{
    qsort(values, TRIALS, sizeof *values, compare_double);
    return (values[TRIALS / 2 - 1] + values[TRIALS / 2]) / 2.0;
}

static int time_variant(int scalar, const uint8_t *src, const uint8_t *initial,
                        uint8_t *dst, uint32_t coefficient, const gf_t *gf,
                        double *seconds)
{
    double start, end;
    int i;

    memcpy(dst, initial, REGION_BYTES);
    start = now_seconds();
    if (start < 0.0) return -1;
    for (i = 0; i < REGION_CALLS; i++) {
        if (scalar)
            mult_XORs_scalar(src, dst, coefficient, REGION_BYTES, gf);
        else
            mult_XORs(src, dst, coefficient, REGION_BYTES, gf);
    }
    end = now_seconds();
    if (end <= start) return -1;
    *seconds = end - start;
    return 0;
}

static int benchmark_field(FILE *csv, int width, uint32_t coefficient)
{
    gf_t gf = { 0 };
    uint8_t *src = NULL, *initial = NULL, *scalar = NULL, *simd = NULL;
    double scalar_times[TRIALS], simd_times[TRIALS];
    double warmup_time, scalar_median, simd_median;
    size_t i;
    int trial, ok = 0;

    if (gf_init(&gf, width) != 0) goto done;
    src = malloc(REGION_BYTES);
    initial = malloc(REGION_BYTES);
    scalar = malloc(REGION_BYTES);
    simd = malloc(REGION_BYTES);
    if (!src || !initial || !scalar || !simd) goto done;
    for (i = 0; i < REGION_BYTES; i++) {
        src[i] = (uint8_t)(i * 37U + 11U);
        initial[i] = (uint8_t)(i * 13U + 7U);
    }

    if (time_variant(1, src, initial, scalar, coefficient, &gf, &warmup_time) != 0 ||
        time_variant(0, src, initial, simd, coefficient, &gf, &warmup_time) != 0)
        goto done;
    for (trial = 0; trial < TRIALS; trial++) {
        if (time_variant(1, src, initial, scalar, coefficient, &gf,
                         &scalar_times[trial]) != 0 ||
            time_variant(0, src, initial, simd, coefficient, &gf,
                         &simd_times[trial]) != 0 ||
            memcmp(scalar, simd, REGION_BYTES) != 0)
            goto done;
    }

    scalar_median = median(scalar_times);
    simd_median = median(simd_times);
    if (fprintf(csv, "%d,%u,%d,%u,%.6f,%.6f,%.3fx,PASS\n",
                width, REGION_BYTES, REGION_CALLS,
                REGION_BYTES * REGION_CALLS,
                scalar_median * 1000.0, simd_median * 1000.0,
                scalar_median / simd_median) < 0)
        goto done;
    printf("GF(2^%d): scalar %.3f ms, SIMD %.3f ms, %.2fx\n",
           width, scalar_median * 1000.0, simd_median * 1000.0,
           scalar_median / simd_median);
    ok = 1;

done:
    if (!ok) fprintf(stderr, "GF(2^%d) SIMD benchmark failed\n", width);
    free(src); free(initial); free(scalar); free(simd);
    gf_free(&gf);
    return ok;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "results/m3_simd_benchmark_results.csv";
    FILE *csv;
    int ok;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [csv-output]\n", argv[0]);
        return 2;
    }
    csv = fopen(path, "w");
    if (!csv) {
        perror(path);
        return 2;
    }
    if (fprintf(csv, "field_width,region_bytes,calls_per_trial,total_bytes,scalar_ms,simd_ms,speedup,status\n") < 0) {
        fclose(csv);
        return 2;
    }
    ok = benchmark_field(csv, 8, 2) &&
         benchmark_field(csv, 16, 0xe357U) &&
         benchmark_field(csv, 32, 0x81234567U);
    if (fclose(csv) != 0) ok = 0;
    if (!ok) return 1;
    printf("SIMD comparison written to %s\n", path);
    return 0;
}

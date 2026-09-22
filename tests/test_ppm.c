#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec.h"
#include "coefficients.h"
#include "ppm.h"
#include "sd_code.h"

#define TEST_SECTOR_BYTES 16

static void fill_data(uint8_t *stripe, size_t sectors, size_t sector_bytes,
                      const char *is_parity)
{
    size_t i, b;

    for (i = 0; i < sectors; i++)
        if (!is_parity[i])
            for (b = 0; b < sector_bytes; b++)
                stripe[i * sector_bytes + b] = (uint8_t)(i * 29U + b * 17U + 3U);
}

static int check_figure3(void)
{
    static const uint32_t coefficients[] = { 1, 2 };
    sd_code_t code = { 4, 4, 1, 1, 8, coefficients };
    int faulty[] = { 2, 6, 10, 13, 14 };
    int parity[8], np, i, ok = 0;
    size_t sectors = (size_t)sd_cols(&code);
    size_t bytes = sectors * TEST_SECTOR_BYTES;
    char *is_parity = NULL;
    uint8_t *golden = NULL, *baseline = NULL, *ppm = NULL;
    gf_t gf = { 0 };
    gf_mat H = { 0 };
    decode_stats_t baseline_stats;
    ppm_stats_t ppm_stats;

    if (gf_init(&gf, 8) != 0 || sd_build_H(&code, &gf, &H) != 0) goto done;
    is_parity = calloc(sectors, 1);
    golden = calloc(1, bytes);
    baseline = malloc(bytes);
    ppm = malloc(bytes);
    if (!is_parity || !golden || !baseline || !ppm) goto done;
    np = sd_parity_sectors(&code, parity);
    if (np != H.rows) goto done;
    for (i = 0; i < np; i++) is_parity[parity[i]] = 1;
    fill_data(golden, sectors, TEST_SECTOR_BYTES, is_parity);
    if (ec_encode(&H, parity, np, golden, TEST_SECTOR_BYTES,
                  &gf, &baseline_stats) != 0)
        goto done;
    memcpy(baseline, golden, bytes);
    memcpy(ppm, golden, bytes);
    for (i = 0; i < 5; i++) {
        memset(baseline + (size_t)faulty[i] * TEST_SECTOR_BYTES,
               0xa5, TEST_SECTOR_BYTES);
        memset(ppm + (size_t)faulty[i] * TEST_SECTOR_BYTES,
               0xa5, TEST_SECTOR_BYTES);
    }

    if (ec_recover(&H, faulty, 5, baseline, TEST_SECTOR_BYTES,
                   &gf, &baseline_stats) != 0 ||
        ppm_recover_sd(&code, &H, faulty, 5, ppm, TEST_SECTOR_BYTES,
                       &gf, 4, &ppm_stats) != 0)
        goto done;
    ok = baseline_stats.mult_xors == 35 && ppm_stats.mult_xors == 29 &&
         ppm_stats.independent_groups == 3 &&
         ppm_stats.dependent_failures == 2 &&
         ppm_stats.mult_xors == ppm_stats.independent_mult_xors +
                                 ppm_stats.remainder_mult_xors &&
         memcmp(baseline, golden, bytes) == 0 &&
         memcmp(ppm, golden, bytes) == 0 &&
         ec_syndrome_is_zero(&H, ppm, TEST_SECTOR_BYTES, &gf);

done:
    if (!ok) fprintf(stderr, "paper Figure 3 PPM test failed\n");
    free(is_parity); free(golden); free(baseline); free(ppm);
    mat_free(&H);
    gf_free(&gf);
    return ok;
}

static int check_configuration(const sd_coeff_entry *entry, int z)
{
    sd_code_t code = {
        entry->n, entry->r, entry->m, entry->s, entry->w, entry->a
    };
    const int thread_counts[] = { 1, 2, 4 };
    int parity[80], faulty[80], np, nf, i, t, ok = 0;
    size_t sectors = (size_t)sd_cols(&code);
    size_t bytes = sectors * TEST_SECTOR_BYTES;
    char *is_parity = NULL;
    uint8_t *golden = NULL, *stripe = NULL;
    uint64_t expected_count = 0;
    gf_t gf = { 0 };
    gf_mat H = { 0 };
    decode_stats_t encode_stats;

    if (gf_init(&gf, code.w) != 0 || sd_build_H(&code, &gf, &H) != 0) goto done;
    is_parity = calloc(sectors, 1);
    golden = calloc(1, bytes);
    stripe = malloc(bytes);
    if (!is_parity || !golden || !stripe) goto done;
    np = sd_parity_sectors(&code, parity);
    nf = sd_failure_sectors(&code, z, faulty);
    if (np != H.rows || nf != H.rows) goto done;
    for (i = 0; i < np; i++) is_parity[parity[i]] = 1;
    fill_data(golden, sectors, TEST_SECTOR_BYTES, is_parity);
    if (ec_encode(&H, parity, np, golden, TEST_SECTOR_BYTES,
                  &gf, &encode_stats) != 0)
        goto done;

    for (t = 0; t < (int)(sizeof thread_counts / sizeof thread_counts[0]); t++) {
        ppm_stats_t stats;
        memcpy(stripe, golden, bytes);
        for (i = 0; i < nf; i++)
            memset(stripe + (size_t)faulty[i] * TEST_SECTOR_BYTES,
                   0xa5, TEST_SECTOR_BYTES);
        if (ppm_recover_sd(&code, &H, faulty, nf, stripe,
                           TEST_SECTOR_BYTES, &gf, thread_counts[t], &stats) != 0 ||
            memcmp(stripe, golden, bytes) != 0 ||
            !ec_syndrome_is_zero(&H, stripe, TEST_SECTOR_BYTES, &gf) ||
            stats.independent_groups != code.r - z ||
            stats.mult_xors != stats.independent_mult_xors +
                                stats.remainder_mult_xors ||
            (t > 0 && stats.mult_xors != expected_count))
            goto done;
        expected_count = stats.mult_xors;
    }
    ok = 1;

done:
    if (!ok)
        fprintf(stderr, "PPM configuration failed: n=%d m=%d s=%d r=%d w=%d z=%d\n",
                code.n, code.m, code.s, code.r, code.w, z);
    free(is_parity); free(golden); free(stripe);
    mat_free(&H);
    gf_free(&gf);
    return ok;
}

static int check_invalid_input(void)
{
    static const uint32_t coefficients[] = { 1, 2 };
    sd_code_t code = { 4, 4, 1, 1, 8, coefficients };
    int faulty[] = { 2, 6, 10, 13, 14 };
    int duplicate[] = { 2, 2, 10, 13, 14 };
    uint8_t stripe[16 * TEST_SECTOR_BYTES] = { 0 };
    gf_t gf = { 0 };
    gf_mat H = { 0 };
    int ok = 0;

    if (gf_init(&gf, 8) != 0 || sd_build_H(&code, &gf, &H) != 0) goto done;
    ok = ppm_recover_sd(&code, &H, duplicate, 5, stripe, TEST_SECTOR_BYTES,
                        &gf, 1, NULL) == -1 &&
         ppm_recover_sd(&code, &H, faulty, 4, stripe, TEST_SECTOR_BYTES,
                        &gf, 1, NULL) == -1 &&
         ppm_recover_sd(&code, &H, faulty, 5, stripe, TEST_SECTOR_BYTES,
                        &gf, 0, NULL) == -1;

done:
    if (!ok) fprintf(stderr, "PPM invalid-input test failed\n");
    mat_free(&H);
    gf_free(&gf);
    return ok;
}

static int check_mat_mul(void)
{
    gf_t gf = { 0 };
    gf_mat A = { 0 }, I = { 0 }, C = { 0 };
    int ok = 0;

    if (gf_init(&gf, 8) != 0 || mat_alloc(&A, 2, 2) != 0 ||
        mat_alloc(&I, 2, 2) != 0)
        goto done;
    MAT(&A, 0, 0) = 1; MAT(&A, 0, 1) = 2;
    MAT(&A, 1, 0) = 3; MAT(&A, 1, 1) = 4;
    MAT(&I, 0, 0) = 1; MAT(&I, 1, 1) = 1;
    gf_count_reset();
    ok = mat_mul(&A, &I, &gf, &C) == 0 &&
         memcmp(A.e, C.e, 4 * sizeof *A.e) == 0 && gf_count_get() == 0;

done:
    if (!ok) fprintf(stderr, "matrix multiplication test failed\n");
    mat_free(&A); mat_free(&I); mat_free(&C);
    gf_free(&gf);
    return ok;
}

int main(void)
{
    sd_coeff_table table = { 0 };
    const sd_coeff_entry *e8, *e16, *e32;
    int ok;

    if (sd_coeff_load("data/FAST-Coefficients.txt", &table) != 0) return 1;
    e8 = sd_coeff_find(&table, 16, 1, 1, 16);
    e16 = sd_coeff_find(&table, 16, 2, 2, 16);
    e32 = sd_coeff_find(&table, 16, 3, 3, 16);
    ok = e8 && e16 && e32 && check_mat_mul() && check_figure3() &&
         check_configuration(e8, 1) && check_configuration(e16, 2) &&
         check_configuration(e32, 3) && check_invalid_input();
    sd_coeff_free(&table);
    if (!ok) return 1;
    printf("PPM partitioning, recovery, and OpenMP tests: PASS\n");
    return 0;
}

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec.h"
#include "ppm.h"
#include "sd_code.h"

#define SECTOR_BYTES 64

static int check_simd_matches_scalar(void)
{
    gf_t gf = { 0 };
    uint8_t src[4103], scalar[4107], vector[4107];
    const uint32_t coefficients[] = { 1, 2, 0x57, 0xe3 };
    size_t i, k;
    int ok = 0;

    if (gf_init(&gf, 8) != 0) return 0;
    for (i = 0; i < sizeof src; i++) src[i] = (uint8_t)(i * 29U + 17U);
    for (i = 0; i < sizeof scalar; i++) scalar[i] = vector[i] = (uint8_t)(i * 11U);
    for (k = 0; k < sizeof coefficients / sizeof coefficients[0]; k++) {
        gf_count_reset();
        mult_XORs_scalar(src + 1, scalar + 3, coefficients[k], 4099, &gf);
        if (gf_count_get() != 1) goto done;
        gf_count_reset();
        mult_XORs(src + 1, vector + 3, coefficients[k], 4099, &gf);
        if (gf_count_get() != 1 || memcmp(scalar, vector, sizeof scalar) != 0)
            goto done;
    }
    ok = 1;

done:
    if (!ok) fprintf(stderr, "M3 SIMD/scalar region comparison failed\n");
    gf_free(&gf);
    return ok;
}

static int check_wide_simd_matches_scalar(int width)
{
    gf_t gf = { 0 };
    uint8_t src[4103], scalar[4107], vector[4107];
    uint32_t coefficient = width == 16 ? 0xe357U : 0x81234567U;
    size_t i;
    int ok = 0;

    if (gf_init(&gf, width) != 0) return 0;
    for (i = 0; i < sizeof src; i++) src[i] = (uint8_t)(i * 43U + 5U);
    for (i = 0; i < sizeof scalar; i++) scalar[i] = vector[i] = (uint8_t)(i * 17U + 3U);

    gf_count_reset();
    mult_XORs_scalar(src + 1, scalar + 3, coefficient, 4096, &gf);
    if (gf_count_get() != 1) goto done;
    gf_count_reset();
    mult_XORs(src + 1, vector + 3, coefficient, 4096, &gf);
    if (gf_count_get() != 1 || memcmp(scalar, vector, sizeof scalar) != 0)
        goto done;
    ok = 1;

done:
    if (!ok) fprintf(stderr, "M3 GF(2^%d) SIMD/scalar comparison failed\n", width);
    gf_free(&gf);
    return ok;
}

static int check_dynamic_recovery(void)
{
    static const uint32_t coefficients[] = { 1, 2 };
    sd_code_t code = { 4, 4, 1, 1, 8, coefficients };
    int faulty[] = { 2, 6, 10, 13, 14 }, parity[8], np, i;
    size_t sectors = (size_t)sd_cols(&code), bytes = sectors * SECTOR_BYTES;
    uint8_t *golden = NULL, *baseline = NULL, *selected = NULL;
    char *is_parity = NULL;
    gf_t gf = { 0 };
    gf_mat H = { 0 };
    decode_stats_t baseline_stats = { 0 };
    ppm_stats_t selected_stats = { 0 };
    int ok = 0;

    if (gf_init(&gf, 8) != 0 || sd_build_H(&code, &gf, &H) != 0) goto done;
    is_parity = calloc(sectors, 1);
    golden = calloc(1, bytes);
    baseline = malloc(bytes);
    selected = malloc(bytes);
    if (!is_parity || !golden || !baseline || !selected) goto done;
    np = sd_parity_sectors(&code, parity);
    if (np != H.rows) goto done;
    for (i = 0; i < np; i++) is_parity[parity[i]] = 1;
    for (i = 0; i < (int)sectors; i++)
        if (!is_parity[i])
            memset(golden + (size_t)i * SECTOR_BYTES, i * 19 + 3, SECTOR_BYTES);
    if (ec_encode(&H, parity, np, golden, SECTOR_BYTES, &gf, &baseline_stats) != 0)
        goto done;
    memcpy(baseline, golden, bytes);
    memcpy(selected, golden, bytes);
    for (i = 0; i < 5; i++) {
        memset(baseline + (size_t)faulty[i] * SECTOR_BYTES, 0xa5, SECTOR_BYTES);
        memset(selected + (size_t)faulty[i] * SECTOR_BYTES, 0xa5, SECTOR_BYTES);
    }
    if (ec_recover(&H, faulty, 5, baseline, SECTOR_BYTES, &gf,
                   &baseline_stats) != 0 ||
        ppm_recover_sd(&code, &H, faulty, 5, selected, SECTOR_BYTES, &gf, 4,
                       &selected_stats) != 0)
        goto done;
    ok = memcmp(baseline, golden, bytes) == 0 &&
         memcmp(selected, golden, bytes) == 0 &&
         ec_syndrome_is_zero(&H, selected, SECTOR_BYTES, &gf) &&
         selected_stats.c_multiply > 0.0 &&
         selected_stats.estimated_cost > 0.0 &&
         selected_stats.mult_xors == selected_stats.independent_mult_xors +
                                     selected_stats.remainder_mult_xors &&
         selected_stats.used_partitioning ==
             (selected_stats.independent_groups > 0);

done:
    if (!ok) fprintf(stderr, "M3 dynamic recovery test failed\n");
    free(golden); free(baseline); free(selected); free(is_parity);
    mat_free(&H);
    gf_free(&gf);
    return ok;
}

static int check_parallel_remainder(void)
{
    gf_t gf = { 0 };
    gf_mat Finv = { 0 }, S = { 0 };
    int faulty[] = { 0, 1 }, surviving[] = { 2, 3, 4 };
    uint8_t serial[5 * 4096], parallel[5 * 4096];
    int actual_threads = 0, i;
    int ok = 0;

    if (gf_init(&gf, 8) != 0 || mat_alloc(&Finv, 2, 2) != 0 ||
        mat_alloc(&S, 2, 3) != 0)
        goto done;
    MAT(&Finv, 0, 0) = 1; MAT(&Finv, 0, 1) = 2;
    MAT(&Finv, 1, 0) = 3; MAT(&Finv, 1, 1) = 4;
    MAT(&S, 0, 0) = 5; MAT(&S, 0, 1) = 6; MAT(&S, 0, 2) = 7;
    MAT(&S, 1, 0) = 8; MAT(&S, 1, 1) = 9; MAT(&S, 1, 2) = 10;
    for (i = 0; i < (int)sizeof serial; i++) serial[i] = parallel[i] = i * 31U + 9U;
    if (ec_decode_normal(&Finv, &S, faulty, surviving, serial, 4096, &gf) != 0 ||
        ec_decode_normal_parallel(&Finv, &S, faulty, surviving, parallel,
                                  4096, &gf, 4, &actual_threads) != 0)
        goto done;
    ok = memcmp(serial, parallel, sizeof serial) == 0 &&
         actual_threads >= 1 && actual_threads <= 4 &&
         gf_count_get() == (uint64_t)(mat_nonzeros(&S) + mat_nonzeros(&Finv));

done:
    if (!ok) fprintf(stderr, "M3 parallel remainder test failed\n");
    mat_free(&Finv); mat_free(&S);
    gf_free(&gf);
    return ok;
}

int main(void)
{
    if (!check_simd_matches_scalar() || !check_dynamic_recovery() ||
        !check_wide_simd_matches_scalar(16) ||
        !check_wide_simd_matches_scalar(32) ||
        !check_parallel_remainder()) return 1;
    puts("M3 cost selection, SIMD, and parallel remainder tests: PASS");
    return 0;
}

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec.h"
#include "sd_code.h"

static int check_upstream_matrix(void)
{
    static const uint32_t coefficients[] = { 1, 42, 26, 61 };
    static const uint32_t row1[] = { 1, 42, 48, 179, 105, 28 };
    static const uint32_t row3[] = { 127, 122, 248, 8, 77, 157 };
    static const uint32_t dense0[] = {
        1, 26, 89, 185, 145, 38, 59, 36, 15, 150, 96, 169,
        44, 223, 100, 193, 85, 1, 26, 89, 185, 145, 38, 59
    };
    sd_code_t code = { 6, 4, 2, 2, 8, coefficients };
    gf_t gf;
    gf_mat H;
    int j, ok = 1;

    if (gf_init(&gf, 8) != 0 || sd_build_H(&code, &gf, &H) != 0) return 0;
    for (j = 0; j < 6; j++) {
        ok &= MAT(&H, 1, j) == row1[j];
        ok &= MAT(&H, 3, 6 + j) == row3[j];
    }
    for (j = 0; j < 24; j++) ok &= MAT(&H, 8, j) == dense0[j];
    if (!ok) fprintf(stderr, "SD matrix differs from upstream FAST construction\n");
    mat_free(&H);
    gf_free(&gf);
    return ok;
}

static int check_multiline_parity(void)
{
    static const uint32_t coefficients[] = { 1, 2, 4, 8, 16, 32 };
    sd_code_t code = { 4, 4, 3, 3, 8, coefficients };
    int parity[16], faulty[16], np, nf, i;

    np = sd_parity_sectors(&code, parity);
    nf = sd_failure_sectors(&code, 3, faulty);
    if (np != 15 || nf != 15 || sd_failure_sectors(&code, 1, faulty) != -1)
        return 0;
    for (i = 0; i < 15; i++) {
        if (parity[i] != i + 1 || faulty[i] != i) {
            fprintf(stderr, "multi-row parity/failure layout mismatch\n");
            return 0;
        }
    }
    return 1;
}

static int check_w32_roundtrip(void)
{
    static const uint32_t coefficients[] = { 1, 2, 4, 8, 16, 32 };
    sd_code_t code = { 16, 16, 3, 3, 32, coefficients };
    const size_t sector_bytes = 8;
    const size_t sectors = (size_t)code.n * code.r;
    const size_t stripe_bytes = sectors * sector_bytes;
    gf_t gf;
    gf_mat H;
    int parity[80], faulty[80], np, nf, i, rc;
    uint8_t *stripe = NULL, *golden = NULL, *is_parity = NULL;
    decode_stats_t stats;
    int ok = 0;

    if (gf_init(&gf, 32) != 0 || sd_build_H(&code, &gf, &H) != 0) return 0;
    stripe = calloc(1, stripe_bytes);
    golden = calloc(1, stripe_bytes);
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
                stripe[(size_t)i * sector_bytes + b] = (uint8_t)(i * 17 + b);
        }

    rc = ec_encode(&H, parity, np, stripe, sector_bytes, &gf, &stats);
    if (rc != 0 || !ec_syndrome_is_zero(&H, stripe, sector_bytes, &gf)) goto done;
    memcpy(golden, stripe, stripe_bytes);
    for (i = 0; i < nf; i++)
        memset(stripe + (size_t)faulty[i] * sector_bytes, 0xa5, sector_bytes);
    rc = ec_recover(&H, faulty, nf, stripe, sector_bytes, &gf, &stats);
    ok = rc == 0 && memcmp(stripe, golden, stripe_bytes) == 0
         && ec_syndrome_is_zero(&H, stripe, sector_bytes, &gf)
         && stats.mult_xors == (uint64_t)(stats.u_S + stats.u_Finv);

done:
    if (!ok) fprintf(stderr, "GF(2^32) SD round trip failed\n");
    free(stripe); free(golden); free(is_parity);
    mat_free(&H);
    gf_free(&gf);
    return ok;
}

int main(void)
{
    if (!check_upstream_matrix() || !check_multiline_parity() || !check_w32_roundtrip())
        return 1;
    printf("SD construction and round-trip tests: PASS\n");
    return 0;
}

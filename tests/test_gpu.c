#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coefficients.h"
#include "codec.h"
#include "gpu_backend.h"
#include "gpu_ppm.h"

/* Force direct and two-stage matvec paths independently of cost selection.
 * Odd symbol counts exercise partially populated CUDA blocks. */
static void test_matvec(gf_t *gf, int variant)
{
    const size_t sb = 1031U * (size_t)(gf->w / 8);
    const int sources[] = {0, 1, 2, 3, 4}, destinations[] = {5, 6, 7, 8};
    uint32_t matrix[20], identity[16] = {0};
    uint8_t *stripe = malloc(9 * sb), *expected = malloc(9 * sb);
    assert(stripe && expected);
    for (size_t i = 0; i < 9 * sb; i++) stripe[i] = (uint8_t)(i * 71U + 19U);
    memcpy(expected, stripe, 9 * sb);
    for (int i = 0; i < 20; i++)
        matrix[i] = i % 5 == 0 ? 0 : i % 5 == 1 ? 1 :
            (uint32_t)((UINT64_C(0xfedcba97) * (i + 1)) % gf->order);
    for (int i = 0; i < 4; i++) {
        identity[4 * i + i] = 1;
        memset(expected + (5 + i) * sb, 0, sb);
        for (int j = 0; j < 5; j++)
            if (matrix[5 * i + j])
                mult_XORs_scalar(expected + j * sb, expected + (5 + i) * sb,
                                 matrix[5 * i + j], sb, gf);
    }
    for (int staged = 0; staged < 2; staged++) {
        gpu_session_t *session = NULL;
        assert(gpu_session_create(&session, stripe, 9 * sb, 4 * sb,
                                  gf->w, variant, gf->logt,
                                  gf->w == 32 ? 0 : (size_t)gf->order,
                                  gf->expt, gf->w == 32 ? 0 : 2 * (size_t)gf->nz + 1,
                                  gf->split8, gf->w == 32 ? 16U * 65536U : 0) == 0);
        assert(gpu_session_matvec(session, matrix, 4, 5, sources, destinations,
                                  sb, 0, staged, 0) == 0);
        if (staged)
            assert(gpu_session_matvec(session, identity, 4, 4, NULL,
                                      destinations, sb, 1, 0, 0) == 0);
        assert(gpu_session_copy_back(session, stripe) == 0);
        assert(memcmp(stripe, expected, 9 * sb) == 0);
        assert(gpu_session_kernel_seconds_sum(session) > 0);
        gpu_session_destroy(session);
    }
    free(stripe);
    free(expected);
}

int main(void)
{
    sd_coeff_table table = {0};
    gf_t fields[3] = {{0}};
    int cases = 0;
    assert(sd_coeff_load("data/FAST-Coefficients.txt", &table) == 0);
    for (int f = 0; f < 3; f++) {
        assert(gf_init(&fields[f], 8 << f) == 0);
        for (int variant = 0; variant < 2; variant++)
            test_matvec(&fields[f], variant);
    }
    for (int size = 4; size <= 24; size += 20)
        for (int m = 1; m <= 3; m++)
            for (int s = 1; s <= 3; s++) {
                const sd_coeff_entry *entry = sd_coeff_find(&table, size, m, s, size);
                assert(entry);
                sd_code_t code = {size, size, m, s, entry->w, entry->a};
                gf_t *gf = &fields[entry->w == 8 ? 0 : entry->w == 16 ? 1 : 2];
                gf_mat H = {0};
                const size_t sb = 16, bytes = (size_t)sd_cols(&code) * sb;
                uint8_t *golden = malloc(bytes), *stripe = malloc(bytes);
                int parity[80], faulty[80];
                assert(golden && stripe && sd_build_H(&code, gf, &H) == 0);
                for (size_t b = 0; b < bytes; b++) golden[b] = (uint8_t)(b * 37U + 11U);
                int np = sd_parity_sectors(&code, parity);
                assert(ec_encode(&H, parity, np, golden, sb, gf, NULL) == 0);
                for (int z = 1; z <= s; z++) {
                    if (s > z * (size - m)) continue;
                    int nf = sd_failure_sectors(&code, z, faulty);
                    assert(nf == H.rows);
                    for (int variant = 0; variant < 2; variant++) {
                        gpu_ppm_stats_t stats = {0};
                        memcpy(stripe, golden, bytes);
                        for (int i = 0; i < nf; i++) memset(stripe + faulty[i] * sb, 0xa5, sb);
                        assert(gpu_ppm_recover_sd(&code, &H, faulty, nf, stripe,
                                                  sb, gf, variant, &stats) == 0);
                        assert(memcmp(stripe, golden, bytes) == 0);
                        assert(ec_syndrome_is_zero(&H, stripe, sb, gf));
                        cases++;
                    }
                }
                mat_free(&H);
                free(golden);
                free(stripe);
            }
    for (int f = 0; f < 3; f++) gf_free(&fields[f]);
    sd_coeff_free(&table);
    printf("GPU tests passed: 12 direct/staged matvec checks and %d SD recoveries.\n", cases);
    return 0;
}

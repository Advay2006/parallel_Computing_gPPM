/* main.c -- Milestone 1 smoke test for the Sec. 2.2 sequential baseline.
 *
 * Reproduces the paper's Figure 2 example, SD^{1,1}_{4,4}(8 | 1, 2):
 * 16 sectors, disk 2 fails (b2, b6, b10, b14) plus one extra faulty sector
 * (b13), giving the faulty set {2, 6, 10, 13, 14} and C1 = 35.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gf.h"
#include "matrix.h"
#include "sd_code.h"
#include "codec.h"

#define SECTOR_BYTES 4096

static uint32_t rng_state = 0x9E3779B9u;
static uint8_t rnd_byte(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (uint8_t)(rng_state & 0xFF);
}

/* Paper Sec. 3.2, the closed form for the traditional normal-sequence decode. */
static long C1_formula(int n, int r, int m, int s, int z)
{
    return (long)n * r * (m + s)
         + (long)m * (m * r + s) * (z - 1)
         + (long)m * m * (r - z);
}

int main(void)
{
    const uint32_t coeff[] = { 1, 2 };                 /* a_0 = 1, a_1 = 2 */
    sd_code_t code = { .n = 4, .r = 4, .m = 1, .s = 1, .w = 8, .a = coeff };

    /* Figure 2's failure scenario: disk 2 lost, plus the extra sector b13. */
    int faulty[] = { 2, 6, 10, 13, 14 };
    const int nf = (int)(sizeof faulty / sizeof faulty[0]);
    const int z  = 1;                                  /* b13 sits in 1 row */

    gf_t   gf;
    gf_mat H;
    int    parity[64], np, i, rc, ok;
    size_t nsec, stripe_bytes;
    uint8_t *stripe = NULL, *golden = NULL;
    char   *is_parity;
    decode_stats_t st;

    if (gf_init(&gf, code.w) != 0) { fprintf(stderr, "gf_init failed\n"); return 1; }

    /* ---- Step 1 ---------------------------------------------------------- */
    if (sd_build_H(&code, &gf, &H) != 0) { fprintf(stderr, "H failed\n"); return 1; }
    printf("SD^{%d,%d}_{%d,%d}(%d | %u, %u)\n",
           code.m, code.s, code.n, code.r, code.w, coeff[0], coeff[1]);
    printf("R_H = %d, C_H = %d, u(H) = %ld\n\n",
           H.rows, H.cols, mat_nonzeros(&H));
    mat_print(&H, "H");

    nsec         = (size_t)sd_cols(&code);
    stripe_bytes = nsec * SECTOR_BYTES;
    stripe = calloc(1, stripe_bytes);
    golden = calloc(1, stripe_bytes);
    is_parity = calloc(nsec, 1);

    /* ---- Encode (Steps 2-4 with faulty set = parity set) ------------------ */
    np = sd_parity_sectors(&code, parity);
    printf("\nparity sectors:");
    for (i = 0; i < np; i++) { printf(" b%d", parity[i]); is_parity[parity[i]] = 1; }
    printf("\n");

    for (i = 0; i < (int)nsec; i++)                    /* random data sectors */
        if (!is_parity[i]) {
            size_t b;
            for (b = 0; b < SECTOR_BYTES; b++)
                stripe[(size_t)i * SECTOR_BYTES + b] = rnd_byte();
        }

    rc = ec_encode(&H, parity, np, stripe, SECTOR_BYTES, &gf, &st);
    if (rc != 0) { fprintf(stderr, "encode: F singular\n"); return 1; }
    printf("encode: u(S) = %ld, u(F^-1) = %ld, mult_XORs = %llu (C1 = %ld)\n",
           st.u_S, st.u_Finv, (unsigned long long)st.mult_xors,
           C1_formula(code.n, code.r, code.m, code.s, 1));

    /* ---- Validation 2A: H * B = 0 ----------------------------------------- */
    ok = ec_syndrome_is_zero(&H, stripe, SECTOR_BYTES, &gf);
    printf("2A  H*B == 0 after encode : %s\n", ok ? "PASS" : "FAIL");
    if (!ok) return 1;

    memcpy(golden, stripe, stripe_bytes);

    /* ---- Decode ----------------------------------------------------------- */
    printf("\nfaulty:");
    for (i = 0; i < nf; i++) printf(" b%d", faulty[i]);
    printf("   (z = %d)\n", z);

    for (i = 0; i < nf; i++)                           /* wipe the lost data */
        memset(stripe + (size_t)faulty[i] * SECTOR_BYTES, 0xAA, SECTOR_BYTES);

    rc = ec_recover(&H, faulty, nf, stripe, SECTOR_BYTES, &gf, &st);
    if (rc != 0) { fprintf(stderr, "decode: F singular\n"); return 1; }

    ok = (memcmp(stripe, golden, stripe_bytes) == 0);
    printf("2A  recovered == original : %s\n", ok ? "PASS" : "FAIL");
    ok &= ec_syndrome_is_zero(&H, stripe, SECTOR_BYTES, &gf);
    printf("2A  H*B == 0 after decode : %s\n", ok ? "PASS" : "FAIL");

    /* ---- Validation 2B: measured mult_XORs vs C1 -------------------------- */
    {
        long c1 = C1_formula(code.n, code.r, code.m, code.s, z);
        printf("\n2B  u(S)             = %ld\n", st.u_S);
        printf("2B  u(F^-1)          = %ld\n", st.u_Finv);
        printf("2B  measured C       = %llu\n", (unsigned long long)st.mult_xors);
        printf("2B  C1 formula       = %ld\n", c1);
        printf("2B  exact match      : %s\n",
               ((long)st.mult_xors == c1) ? "PASS" : "FAIL");
        ok &= ((long)st.mult_xors == c1);
    }

    mat_free(&H); gf_free(&gf);
    free(stripe); free(golden); free(is_parity);

    printf("\n%s\n", ok ? "ALL CHECKS PASSED" : "FAILURES PRESENT");
    return ok ? 0 : 1;
}

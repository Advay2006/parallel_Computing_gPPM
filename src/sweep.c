/* sweep.c -- Milestone 1 validation 2B (and 2A) across the paper's full
 * parameter range: 4<=n<=24, 4<=r<=24, 1<=m<=3, 1<=s<=3, 1<=z<=s.
 *
 * Driven by data/FAST-Coefficients.txt (Plank, UT-CS-13-704), whose lines are
 *      n m s r w a_0 ... a_{m+s-1}
 * and which covers the (n,m,s,r) grid completely: 21*3*3*21 = 3969 configs.
 * Crossed with 1<=z<=s that is 7938 sweep points.
 *
 * For each point:
 *   2B  measured mult_XORs during Step 4  ==  C1 (paper Sec. 3.2)?
 *   2A  encode -> H*B==0 -> wipe -> decode -> byte-identical to the original?
 *
 * Usage: ./sweep [coefficients-file] [csv-out]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gf.h"
#include "matrix.h"
#include "sd_code.h"
#include "codec.h"

#define SECTOR_BYTES 16          /* even (w=16 needs it); counts are size-independent */
#define MAX_COEFF    8

/* Paper Sec. 3.2 */
static long C1_formula(int n, int r, int m, int s, int z)
{
    return (long)n * r * (m + s)
         + (long)m * (m * r + s) * (z - 1)
         + (long)m * m * (r - z);
}

/* C1 splits into its two halves, C1 = u(S) + u(F^-1).  Derived algebraically
 * from the formula above and confirmed against the whole sweep:
 *   u(S) is purely structural -- it counts nonzeros of H on surviving columns,
 *        which depends only on the sparse/dense row structure, so it is EXACT.
 *   u(F^-1) is a generic-position estimate: it assumes no entry of the inverse
 *        cancels to zero.  Real coefficient sets sometimes cancel, so the
 *        measured value can be LOWER.  It can never be higher. */
static long pred_uS(int n, int r, int m, int s)
{
    return (long)(m + s) * ((long)n * r - (long)m * r - s);
}
static long pred_uFinv(int r, int m, int s, int z)
{
    return (long)(m * r + s) * (m * z + s) + (long)m * m * (r - z);
}

/* Deterministic failure pattern: the m leftmost disks fail entirely, and the s
 * extra faulty sectors are spread over stripe rows 0..z-1 -- as evenly as
 * possible, packed left to right from the first surviving disk.  Exactly z rows
 * carry extra faults, which is what C1's z means.
 *
 * Feasible iff s <= z*(n-m) and z <= r.  Returns the number of faulty sectors
 * (must be m*r+s = R_H), or -1 if the geometry does not fit.
 */
static int make_faulty(int n, int r, int m, int s, int z, int *f)
{
    int k = 0, i, j, row, base, rem, cnt;

    if (z > r || s > z * (n - m)) return -1;

    for (i = 0; i < r; i++)                    /* the m failed disks */
        for (j = 0; j < m; j++)
            f[k++] = i * n + j;

    base = s / z;
    rem  = s % z;
    for (row = 0; row < z; row++) {            /* extra faulty sectors */
        cnt = base + (row < rem ? 1 : 0);
        for (j = 0; j < cnt; j++)
            f[k++] = row * n + (m + j);
    }

    for (i = 0; i < k; i++)                    /* insertion sort, ascending */
        for (j = i + 1; j < k; j++)
            if (f[j] < f[i]) { int t = f[i]; f[i] = f[j]; f[j] = t; }

    return k;
}

static uint32_t rng = 0x9E3779B9u;
static uint8_t rnd_byte(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (uint8_t)(rng & 0xFF);
}

int main(int argc, char **argv)
{
    const char *coeff_path = (argc > 1) ? argv[1] : "data/FAST-Coefficients.txt";
    const char *csv_path   = (argc > 2) ? argv[2] : "sweep_results.csv";

    FILE *fp = fopen(coeff_path, "r"), *csv;
    char line[512];
    gf_t gf8, gf16;

    long cfg_total = 0, cfg_w32 = 0;
    long pts = 0, attempted = 0, match = 0, under = 0, over = 0;
    long singular = 0, infeasible = 0, skipped_w32 = 0;
    long rt_pass = 0, rt_fail = 0, rt_skip = 0;
    long uS_ok = 0, uS_bad = 0, uF_ok = 0, uF_bad = 0;
    int  shown = 0;

    if (!fp) { fprintf(stderr, "cannot open %s\n", coeff_path); return 2; }
    csv = fopen(csv_path, "w");
    if (!csv) { fprintf(stderr, "cannot write %s\n", csv_path); return 2; }
    fprintf(csv, "n,m,s,r,w,z,u_S,pred_u_S,u_Finv,pred_u_Finv,measured,C1,delta,count_status,roundtrip\n");

    if (gf_init(&gf8, 8) != 0 || gf_init(&gf16, 16) != 0) {
        fprintf(stderr, "gf_init failed\n"); return 2;
    }

    printf("sweep: %s\n\n", coeff_path);

    while (fgets(line, sizeof line, fp)) {
        int n, m, s, r, w, nc, i, z;
        uint32_t a[MAX_COEFF];
        char *p; int consumed;
        gf_mat H;
        const gf_t *gf;

        if (sscanf(line, " %d %d %d %d %d%n", &n, &m, &s, &r, &w, &consumed) != 5)
            continue;                                   /* blank / comment */
        nc = m + s;
        if (nc > MAX_COEFF) continue;
        p = line + consumed;
        for (i = 0; i < nc; i++) {
            if (sscanf(p, " %u%n", &a[i], &consumed) != 1) break;
            p += consumed;
        }
        if (i != nc) {                                  /* no coefficients listed */
            fprintf(stderr, "warn: incomplete coefficients: %s", line);
            continue;
        }

        cfg_total++;
        pts += s;                                       /* z = 1..s */

        if (w == 32) {                                  /* no GF(2^32) backend yet */
            cfg_w32++; skipped_w32 += s;
            for (z = 1; z <= s; z++)
                fprintf(csv, "%d,%d,%d,%d,%d,%d,,,,,,%ld,,SKIP_W32,SKIP\n",
                        n, m, s, r, w, z, C1_formula(n, r, m, s, z));
            continue;
        }
        gf = (w == 8) ? &gf8 : &gf16;

        {
            sd_code_t code = { n, r, m, s, w, a };
            if (sd_build_H(&code, gf, &H) != 0) { fprintf(stderr, "H alloc\n"); return 2; }

            for (z = 1; z <= s; z++) {
                int faulty[24 * 3 + 3], nf, parity[24 * 3 + 3], np, rt_ok = -1;
                size_t nsec = (size_t)n * r, sb = nsec * SECTOR_BYTES;
                uint8_t *stripe, *golden;
                decode_stats_t st;
                long c1 = C1_formula(n, r, m, s, z), delta;
                const char *cstat;

                nf = make_faulty(n, r, m, s, z, faulty);
                if (nf < 0) {
                    infeasible++;
                    fprintf(csv, "%d,%d,%d,%d,%d,%d,,,,,,%ld,,INFEASIBLE,SKIP\n",
                            n, m, s, r, w, z, c1);
                    continue;
                }

                stripe = calloc(1, sb);
                golden = calloc(1, sb);

                /* --- 2A round-trip, where the parity layout fits (s <= n-m) --- */
                np = sd_parity_sectors(&code, parity);
                if (np > 0) {
                    char *is_par = calloc(nsec, 1);
                    size_t b; int q;
                    for (q = 0; q < np; q++) is_par[parity[q]] = 1;
                    for (q = 0; q < (int)nsec; q++)
                        if (!is_par[q])
                            for (b = 0; b < SECTOR_BYTES; b++)
                                stripe[(size_t)q * SECTOR_BYTES + b] = rnd_byte();
                    free(is_par);

                    if (ec_encode(&H, parity, np, stripe, SECTOR_BYTES, gf, &st) == 0
                        && ec_syndrome_is_zero(&H, stripe, SECTOR_BYTES, gf)) {
                        memcpy(golden, stripe, sb);
                        for (q = 0; q < nf; q++)
                            memset(stripe + (size_t)faulty[q] * SECTOR_BYTES,
                                   0xAA, SECTOR_BYTES);
                        rt_ok = 0;                       /* armed; verified below */
                    }
                }

                /* --- 2B count --- */
                if (ec_recover(&H, faulty, nf, stripe, SECTOR_BYTES, gf, &st) != 0) {
                    singular++;
                    fprintf(csv, "%d,%d,%d,%d,%d,%d,,,,,,%ld,,SINGULAR,SKIP\n",
                            n, m, s, r, w, z, c1);
                    free(stripe); free(golden);
                    continue;
                }

                attempted++;
                if (st.u_S    == pred_uS(n, r, m, s))       uS_ok++; else uS_bad++;
                if (st.u_Finv == pred_uFinv(r, m, s, z))    uF_ok++; else uF_bad++;
                delta = (long)st.mult_xors - c1;
                if      (delta == 0) { match++; cstat = "MATCH"; }
                else if (delta <  0) { under++; cstat = "UNDER"; }
                else                 { over++;  cstat = "OVER";  }

                if (rt_ok == 0) {
                    rt_ok = (memcmp(stripe, golden, sb) == 0)
                            && ec_syndrome_is_zero(&H, stripe, SECTOR_BYTES, gf);
                    if (rt_ok) rt_pass++; else rt_fail++;
                } else {
                    rt_skip++;
                }

                fprintf(csv, "%d,%d,%d,%d,%d,%d,%ld,%ld,%ld,%ld,%llu,%ld,%ld,%s,%s\n",
                        n, m, s, r, w, z,
                        st.u_S,    pred_uS(n, r, m, s),
                        st.u_Finv, pred_uFinv(r, m, s, z),
                        (unsigned long long)st.mult_xors, c1, delta, cstat,
                        rt_ok < 0 ? "SKIP" : (rt_ok ? "PASS" : "FAIL"));

                if (delta != 0 && shown < 20) {
                    printf("  %-5s SD^{%d,%d}_{%2d,%2d} w=%2d z=%d : "
                           "measured %6llu  C1 %6ld  delta %+ld\n",
                           cstat, m, s, n, r, w, z,
                           (unsigned long long)st.mult_xors, c1, delta);
                    shown++;
                }

                free(stripe); free(golden);
            }
            mat_free(&H);
        }
    }

    fclose(fp); fclose(csv);
    gf_free(&gf8); gf_free(&gf16);

    printf("\n=== configurations ===\n");
    printf("  in table                : %ld\n", cfg_total);
    printf("  needing GF(2^32)        : %ld  (no backend yet)\n", cfg_w32);
    printf("\n=== sweep points (config x z) ===\n");
    printf("  total (sum of s)        : %ld\n", pts);
    printf("  skipped, w=32           : %ld\n", skipped_w32);
    printf("  skipped, geometry       : %ld  (s > z*(n-m), pattern cannot exist)\n", infeasible);
    printf("  F singular              : %ld\n", singular);
    printf("  evaluated               : %ld\n", attempted);
    printf("\n=== 2B  measured mult_XORs vs C1 ===\n");
    printf("  exact match             : %ld\n", match);
    printf("  under (measured < C1)   : %ld\n", under);
    printf("  over  (measured > C1)   : %ld\n", over);
    if (attempted)
        printf("  match rate              : %.2f%%\n", 100.0 * match / attempted);
    printf("\n=== where C1 is exact, term by term (C1 = u(S) + u(F^-1)) ===\n");
    printf("  u(S)    == (m+s)(nr-mr-s)        : %ld ok, %ld off\n", uS_ok, uS_bad);
    printf("  u(F^-1) == (mr+s)(mz+s)+m^2(r-z) : %ld ok, %ld off  <- all deviation lives here\n",
           uF_ok, uF_bad);

    printf("\n=== 2A  encode/decode round-trip ===\n");
    printf("  pass                    : %ld\n", rt_pass);
    printf("  fail                    : %ld\n", rt_fail);
    printf("  not applicable (s>n-m)  : %ld\n", rt_skip);
    printf("\nper-point results: %s\n", csv_path);

    printf("\n%s\n", (over == 0 && rt_fail == 0 && singular == 0)
           ? "no OVER counts, no round-trip failures, no singular F"
           : "** INVESTIGATE: over-counts, round-trip failures or singular F present **");

    return (under || over || rt_fail || singular) ? 1 : 0;
}

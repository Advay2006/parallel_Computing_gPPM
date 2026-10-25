/* Full Milestone 1 correctness and operation-count sweep for SD codes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coefficients.h"
#include "codec.h"
#include "gf.h"
#include "matrix.h"
#include "sd_code.h"

#define SECTOR_BYTES 16

static long C1_formula(int n, int r, int m, int s, int z)
{
    return (long)n * r * (m + s)
         + (long)m * (m * r + s) * (z - 1)
         + (long)m * m * (r - z);
}

static long pred_uS(int n, int r, int m, int s)
{
    return (long)(m + s) * ((long)n * r - (long)m * r - s);
}

static long pred_uFinv(int r, int m, int s, int z)
{
    return (long)(m * r + s) * (m * z + s) + (long)m * m * (r - z);
}

static uint32_t rng = 0x9e3779b9U;

static uint8_t rnd_byte(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (uint8_t)rng;
}

int main(int argc, char **argv)
{
    const char *coeff_path = argc > 1 ? argv[1] : "data/FAST-Coefficients.txt";
    const char *csv_path = argc > 2 ? argv[2] : "results/sweep_results.csv";
    sd_coeff_table table = { 0 };
    gf_t gf8 = { 0 }, gf16 = { 0 }, gf32 = { 0 };
    FILE *csv = NULL;
    long pts = 0, attempted = 0, match = 0, under = 0, over = 0;
    long singular = 0, infeasible = 0, encode_fail = 0;
    long rt_pass = 0, rt_fail = 0, identity_bad = 0;
    long uS_ok = 0, uS_bad = 0, uF_ok = 0, uF_bad = 0;
    long fields[3] = { 0, 0, 0 };
    int shown = 0, exit_status = 2;
    size_t config_index;

    if (sd_coeff_load(coeff_path, &table) != 0) goto done;
    csv = fopen(csv_path, "w");
    if (!csv) {
        fprintf(stderr, "cannot write %s\n", csv_path);
        goto done;
    }
    fprintf(csv, "n,m,s,r,w,z,u_S,pred_u_S,u_Finv,pred_u_Finv,"
                 "measured,C1,delta,count_status,roundtrip\n");

    if (gf_init(&gf8, 8) != 0 || gf_init(&gf16, 16) != 0 ||
        gf_init(&gf32, 32) != 0) {
        fprintf(stderr, "cannot initialize Galois fields\n");
        goto done;
    }

    printf("sweep: %s\n\n", coeff_path);
    for (config_index = 0; config_index < table.count; config_index++) {
        const sd_coeff_entry *entry = &table.entries[config_index];
        const gf_t *gf = entry->w == 8 ? &gf8 : entry->w == 16 ? &gf16 : &gf32;
        sd_code_t code = {
            entry->n, entry->r, entry->m, entry->s, entry->w, entry->a
        };
        gf_mat H = { 0 };
        int z;

        fields[entry->w == 8 ? 0 : entry->w == 16 ? 1 : 2]++;
        pts += entry->s;
        if (sd_build_H(&code, gf, &H) != 0) {
            fprintf(stderr, "cannot build H for n=%d m=%d s=%d r=%d w=%d\n",
                    entry->n, entry->m, entry->s, entry->r, entry->w);
            goto done;
        }

        for (z = 1; z <= entry->s; z++) {
            int faulty[80], parity[80], nf, np, i, encode_ok, roundtrip_ok;
            size_t sectors = (size_t)entry->n * entry->r;
            size_t stripe_bytes = sectors * SECTOR_BYTES;
            uint8_t *stripe = NULL, *golden = NULL, *is_parity = NULL;
            decode_stats_t stats;
            long c1 = C1_formula(entry->n, entry->r, entry->m, entry->s, z);
            long delta;
            const char *count_status;

            nf = sd_failure_sectors(&code, z, faulty);
            if (nf < 0) {
                infeasible++;
                fprintf(csv, "%d,%d,%d,%d,%d,%d,,,,,,%ld,,INFEASIBLE,SKIP\n",
                        entry->n, entry->m, entry->s, entry->r, entry->w, z, c1);
                continue;
            }

            stripe = calloc(1, stripe_bytes);
            golden = calloc(1, stripe_bytes);
            is_parity = calloc(sectors, 1);
            if (!stripe || !golden || !is_parity) {
                fprintf(stderr, "stripe allocation failed\n");
                free(stripe); free(golden); free(is_parity);
                mat_free(&H);
                goto done;
            }

            np = sd_parity_sectors(&code, parity);
            if (np != sd_rows(&code)) {
                fprintf(stderr, "invalid parity layout\n");
                free(stripe); free(golden); free(is_parity);
                mat_free(&H);
                goto done;
            }
            for (i = 0; i < np; i++) is_parity[parity[i]] = 1;
            for (i = 0; i < (int)sectors; i++)
                if (!is_parity[i]) {
                    size_t b;
                    for (b = 0; b < SECTOR_BYTES; b++)
                        stripe[(size_t)i * SECTOR_BYTES + b] = rnd_byte();
                }

            encode_ok = ec_encode(&H, parity, np, stripe, SECTOR_BYTES, gf, &stats) == 0
                     && ec_syndrome_is_zero(&H, stripe, SECTOR_BYTES, gf);
            if (encode_ok) {
                memcpy(golden, stripe, stripe_bytes);
                for (i = 0; i < nf; i++)
                    memset(stripe + (size_t)faulty[i] * SECTOR_BYTES,
                           0xa5, SECTOR_BYTES);
            } else {
                encode_fail++;
            }

            if (ec_recover(&H, faulty, nf, stripe, SECTOR_BYTES, gf, &stats) != 0) {
                singular++;
                rt_fail++;
                fprintf(csv, "%d,%d,%d,%d,%d,%d,,,,,,%ld,,SINGULAR,FAIL\n",
                        entry->n, entry->m, entry->s, entry->r, entry->w, z, c1);
                free(stripe); free(golden); free(is_parity);
                continue;
            }

            attempted++;
            if (stats.u_S == pred_uS(entry->n, entry->r, entry->m, entry->s))
                uS_ok++;
            else
                uS_bad++;
            if (stats.u_Finv == pred_uFinv(entry->r, entry->m, entry->s, z))
                uF_ok++;
            else
                uF_bad++;
            if (stats.mult_xors != (uint64_t)(stats.u_S + stats.u_Finv))
                identity_bad++;

            delta = (long)stats.mult_xors - c1;
            if (delta == 0) { match++; count_status = "MATCH"; }
            else if (delta < 0) { under++; count_status = "UNDER"; }
            else { over++; count_status = "OVER"; }

            roundtrip_ok = encode_ok
                        && memcmp(stripe, golden, stripe_bytes) == 0
                        && ec_syndrome_is_zero(&H, stripe, SECTOR_BYTES, gf);
            if (roundtrip_ok) rt_pass++; else rt_fail++;

            fprintf(csv, "%d,%d,%d,%d,%d,%d,%ld,%ld,%ld,%ld,%llu,%ld,%ld,%s,%s\n",
                    entry->n, entry->m, entry->s, entry->r, entry->w, z,
                    stats.u_S, pred_uS(entry->n, entry->r, entry->m, entry->s),
                    stats.u_Finv, pred_uFinv(entry->r, entry->m, entry->s, z),
                    (unsigned long long)stats.mult_xors, c1, delta, count_status,
                    roundtrip_ok ? "PASS" : "FAIL");

            if (delta != 0 && shown < 20) {
                printf("  %-5s SD^{%d,%d}_{%2d,%2d} w=%2d z=%d : "
                       "measured %6llu  C1 %6ld  delta %+ld\n",
                       count_status, entry->m, entry->s, entry->n, entry->r,
                       entry->w, z, (unsigned long long)stats.mult_xors, c1, delta);
                shown++;
            }

            free(stripe); free(golden); free(is_parity);
        }
        mat_free(&H);
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

    printf("\n=== configurations ===\n");
    printf("  in table                : %zu\n", table.count);
    printf("  GF(2^8) / GF(2^16) / GF(2^32): %ld / %ld / %ld\n",
           fields[0], fields[1], fields[2]);
    printf("\n=== sweep points (configuration x z) ===\n");
    printf("  total                   : %ld\n", pts);
    printf("  geometry-infeasible     : %ld\n", infeasible);
    printf("  evaluated               : %ld\n", attempted);
    printf("  singular F              : %ld\n", singular);
    printf("\n=== operation counts ===\n");
    printf("  exact C1 match          : %ld\n", match);
    printf("  under C1                : %ld  (coefficient cancellation)\n", under);
    printf("  over C1                 : %ld\n", over);
    printf("  C != u(S)+u(F^-1)       : %ld\n", identity_bad);
    printf("  u(S) exact              : %ld ok, %ld off\n", uS_ok, uS_bad);
    printf("  generic u(F^-1) exact   : %ld ok, %ld under\n", uF_ok, uF_bad);
    printf("\n=== encode/decode correctness ===\n");
    printf("  encode failures         : %ld\n", encode_fail);
    printf("  round-trip pass         : %ld\n", rt_pass);
    printf("  round-trip fail         : %ld\n", rt_fail);
    printf("\nper-point results: %s\n", csv_path);

    exit_status = over || singular || encode_fail || rt_fail || identity_bad || uS_bad
               || attempted != pts - infeasible ? 1 : 0;
    printf("\n%s\n", exit_status == 0
           ? "FULL SWEEP PASSED"
           : "FULL SWEEP FAILED: investigate correctness errors above");

done:
    if (csv) fclose(csv);
    gf_free(&gf8); gf_free(&gf16); gf_free(&gf32);
    sd_coeff_free(&table);
    return exit_status;
}

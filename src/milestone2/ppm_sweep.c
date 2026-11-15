/* Full Milestone 2 PPM correctness and thread-count invariance sweep. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coefficients.h"
#include "codec.h"
#include "ppm.h"

#define SECTOR_BYTES 16
#define PARALLEL_THREADS 4

static uint32_t rng = 0x9e3779b9U;

static uint8_t rnd_byte(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (uint8_t)rng;
}

int main(int argc, char **argv)
{
    const char *coeff_path = argc > 1 ? argv[1] : "data/FAST-Coefficients.txt";
    const char *csv_path = argc > 2 ? argv[2] : "results/ppm_sweep_results.csv";
    sd_coeff_table table = { 0 };
    gf_t gf8 = { 0 }, gf16 = { 0 }, gf32 = { 0 };
    FILE *csv = NULL;
    long points = 0, infeasible = 0, passed = 0, failed = 0;
    size_t config_index;
    int status = 2;

    if (sd_coeff_load(coeff_path, &table) != 0) goto done;
    if (gf_init(&gf8, 8) != 0 || gf_init(&gf16, 16) != 0 ||
        gf_init(&gf32, 32) != 0)
        goto done;
    csv = fopen(csv_path, "w");
    if (!csv) goto done;
    fprintf(csv, "n,m,s,r,w,z,independent_groups,dependent_failures,"
                 "C_t1,C_t4,effective_t4,bytes_equal,syndrome,status\n");

    for (config_index = 0; config_index < table.count; config_index++) {
        const sd_coeff_entry *entry = &table.entries[config_index];
        const gf_t *gf = entry->w == 8 ? &gf8 : entry->w == 16 ? &gf16 : &gf32;
        sd_code_t code = {
            entry->n, entry->r, entry->m, entry->s, entry->w, entry->a
        };
        gf_mat H = { 0 };
        int z;

        if (sd_build_H(&code, gf, &H) != 0) goto done;
        for (z = 1; z <= code.s; z++) {
            int parity[80], faulty[80], np, nf, i;
            size_t sectors = (size_t)sd_cols(&code);
            size_t bytes = sectors * SECTOR_BYTES;
            uint8_t *golden = NULL, *serial = NULL, *parallel = NULL;
            char *is_parity = NULL;
            decode_stats_t encode_stats;
            ppm_stats_t serial_stats = { 0 }, parallel_stats = { 0 };
            int bytes_equal = 0, syndrome = 0, point_ok = 0;

            points++;
            nf = sd_failure_sectors(&code, z, faulty);
            if (nf < 0) {
                infeasible++;
                fprintf(csv, "%d,%d,%d,%d,%d,%d,,,,,,,INFEASIBLE,SKIP\n",
                        code.n, code.m, code.s, code.r, code.w, z);
                continue;
            }

            golden = calloc(1, bytes);
            serial = malloc(bytes);
            parallel = malloc(bytes);
            is_parity = calloc(sectors, 1);
            if (!golden || !serial || !parallel || !is_parity) {
                free(golden); free(serial); free(parallel); free(is_parity);
                mat_free(&H);
                goto done;
            }
            np = sd_parity_sectors(&code, parity);
            if (np != H.rows || nf != H.rows) goto point_done;
            for (i = 0; i < np; i++) is_parity[parity[i]] = 1;
            for (i = 0; i < (int)sectors; i++)
                if (!is_parity[i]) {
                    size_t b;
                    for (b = 0; b < SECTOR_BYTES; b++)
                        golden[(size_t)i * SECTOR_BYTES + b] = rnd_byte();
                }
            if (ec_encode(&H, parity, np, golden, SECTOR_BYTES,
                          gf, &encode_stats) != 0)
                goto point_done;
            memcpy(serial, golden, bytes);
            memcpy(parallel, golden, bytes);
            for (i = 0; i < nf; i++) {
                memset(serial + (size_t)faulty[i] * SECTOR_BYTES,
                       0xa5, SECTOR_BYTES);
                memset(parallel + (size_t)faulty[i] * SECTOR_BYTES,
                       0xa5, SECTOR_BYTES);
            }
            if (ppm_recover_sd(&code, &H, faulty, nf, serial, SECTOR_BYTES,
                               gf, 1, &serial_stats) != 0 ||
                ppm_recover_sd(&code, &H, faulty, nf, parallel, SECTOR_BYTES,
                               gf, PARALLEL_THREADS, &parallel_stats) != 0)
                goto point_done;

            bytes_equal = memcmp(serial, golden, bytes) == 0 &&
                          memcmp(parallel, golden, bytes) == 0 &&
                          memcmp(serial, parallel, bytes) == 0;
            syndrome = ec_syndrome_is_zero(&H, serial, SECTOR_BYTES, gf) &&
                       ec_syndrome_is_zero(&H, parallel, SECTOR_BYTES, gf);
            point_ok = bytes_equal && syndrome &&
                       serial_stats.independent_groups == code.r - z &&
                       parallel_stats.independent_groups == code.r - z &&
                       serial_stats.mult_xors == parallel_stats.mult_xors &&
                       serial_stats.mult_xors ==
                           serial_stats.independent_mult_xors +
                           serial_stats.remainder_mult_xors &&
                       parallel_stats.mult_xors ==
                           parallel_stats.independent_mult_xors +
                           parallel_stats.remainder_mult_xors;

point_done:
            if (point_ok) passed++; else failed++;
            fprintf(csv, "%d,%d,%d,%d,%d,%d,%d,%d,%llu,%llu,%d,%s,%s,%s\n",
                    code.n, code.m, code.s, code.r, code.w, z,
                    serial_stats.independent_groups,
                    serial_stats.dependent_failures,
                    (unsigned long long)serial_stats.mult_xors,
                    (unsigned long long)parallel_stats.mult_xors,
                    parallel_stats.effective_threads,
                    bytes_equal ? "PASS" : "FAIL",
                    syndrome ? "PASS" : "FAIL",
                    point_ok ? "PASS" : "FAIL");
            free(golden); free(serial); free(parallel); free(is_parity);
        }
        mat_free(&H);
    }

    if (ferror(csv) || fclose(csv) != 0) {
        csv = NULL;
        goto done;
    }
    csv = NULL;
    printf("PPM sweep: %ld points, %ld infeasible, %ld passed, %ld failed\n",
           points, infeasible, passed, failed);
    printf("Per-point results: %s\n", csv_path);
    status = failed == 0 && passed == points - infeasible ? 0 : 1;

done:
    if (csv) fclose(csv);
    gf_free(&gf8); gf_free(&gf16); gf_free(&gf32);
    sd_coeff_free(&table);
    return status;
}

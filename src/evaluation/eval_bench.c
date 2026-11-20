#define _POSIX_C_SOURCE 200809L

/* Evaluation driver for the thread-scaling, ablation and paper-reproduction
 * experiments.  The same source is compiled against each decoder checkpoint:
 *   EVAL_M2        Milestone 2 PPM (fixed matrix-first + normal remainder)
 *   (default)      Milestone 3 gPPM (cost-selected sequences)
 * and against scalar or SIMD region arithmetic (M3_DISABLE_SIMD / gf.c choice).
 * Every trial is validated against the encoded stripe. */
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "coefficients.h"
#include "codec.h"
#include "ppm.h"

#ifndef EVAL_BUILD
#define EVAL_BUILD "unknown"
#endif
#define MAX_T 64
#define MAX_VARIANTS (2 * MAX_T + 1)

typedef struct { int threads; int remainder_parallel; } variant_t;

static uint32_t order_rng = 20261120U;
static uint32_t random32(uint32_t *state)
{
    *state ^= *state << 13; *state ^= *state >> 17; *state ^= *state << 5;
    return *state;
}
static double seconds(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) { perror("clock_gettime"); exit(2); }
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static int number(const char *s, int lo, int hi)
{
    char *end; long n;
    errno = 0; n = strtol(s, &end, 10);
    if (errno || end == s || *end || n < lo || n > hi) return -1;
    return (int)n;
}
/* "1-12,16" -> list of thread counts. */
static int parse_threads(const char *s, int *out)
{
    int count = 0;
    char buf[256], *tok, *save = NULL;
    if (strlen(s) >= sizeof buf) return -1;
    strcpy(buf, s);
    for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char *dash = strchr(tok, '-');
        int lo, hi, t;
        if (dash) { *dash = 0; lo = number(tok, 1, MAX_T); hi = number(dash + 1, 1, MAX_T); }
        else lo = hi = number(tok, 1, MAX_T);
        if (lo < 0 || hi < lo) return -1;
        for (t = lo; t <= hi; t++) { if (count == MAX_T) return -1; out[count++] = t; }
    }
    return count;
}

int main(int argc, char **argv)
{
    const char *csv = NULL, *coeff = "data/FAST-Coefficients.txt", *tag = "";
    int n = 16, r = 16, m = 2, s = 1, z = 1, mib = 32, trials = 5;
    int thread_list[MAX_T], nthreads = 1, remainder_mode = 0, baseline = 0;
    int append = 0, opt, i, trial, rc = 2, nv = 0;
    variant_t variants[MAX_VARIANTS];
    sd_coeff_table table = { 0 };
    const sd_coeff_entry *entry;
    gf_t gf = { 0 };
    gf_mat H = { 0 };
    FILE *out = NULL;
    uint8_t *gold = NULL, *work = NULL, *is_parity = NULL;
    int *parity = NULL, *faulty = NULL, np, nf;
    size_t bytes, sector, data_bytes;
    decode_stats_t enc = { 0 };
    static const struct option options[] = {
        {"csv", required_argument, NULL, 'o'}, {"append", no_argument, NULL, 'a'},
        {"trials", required_argument, NULL, 't'}, {"stripe-mib", required_argument, NULL, 'b'},
        {"n", required_argument, NULL, 'n'}, {"r", required_argument, NULL, 'r'},
        {"m", required_argument, NULL, 'm'}, {"s", required_argument, NULL, 's'},
        {"z", required_argument, NULL, 'z'}, {"threads", required_argument, NULL, 'j'},
        {"remainder", required_argument, NULL, 'R'}, {"baseline", no_argument, NULL, 'B'},
        {"tag", required_argument, NULL, 'g'}, {"coefficients", required_argument, NULL, 'c'},
        {NULL, 0, NULL, 0}
    };
    thread_list[0] = 1;
    while ((opt = getopt_long(argc, argv, "", options, NULL)) != -1) {
        switch (opt) {
        case 'o': csv = optarg; break;
        case 'a': append = 1; break;
        case 't': if ((trials = number(optarg, 1, 100)) < 0) goto usage; break;
        case 'b': if ((mib = number(optarg, 1, 512)) < 0) goto usage; break;
        case 'n': if ((n = number(optarg, 4, 24)) < 0) goto usage; break;
        case 'r': if ((r = number(optarg, 4, 24)) < 0) goto usage; break;
        case 'm': if ((m = number(optarg, 1, 3)) < 0) goto usage; break;
        case 's': if ((s = number(optarg, 1, 3)) < 0) goto usage; break;
        case 'z': if ((z = number(optarg, 1, 3)) < 0) goto usage; break;
        case 'j': if ((nthreads = parse_threads(optarg, thread_list)) <= 0) goto usage; break;
        case 'R':
            if (!strcmp(optarg, "serial")) remainder_mode = 0;
            else if (!strcmp(optarg, "parallel")) remainder_mode = 1;
            else if (!strcmp(optarg, "both")) remainder_mode = 2;
            else goto usage;
            break;
        case 'B': baseline = 1; break;
        case 'g': tag = optarg; break;
        case 'c': coeff = optarg; break;
        default: goto usage;
        }
    }
    if (optind != argc || !csv || z > s) goto usage;
#ifdef EVAL_M2
    if (remainder_mode != 0) {
        fprintf(stderr, "M2 always recovers the remainder serially; use --remainder serial.\n");
        goto done;
    }
#endif
    if (baseline) { variants[nv].threads = 0; variants[nv++].remainder_parallel = 0; }
    for (i = 0; i < nthreads; i++) {
        if (remainder_mode != 1) { variants[nv].threads = thread_list[i]; variants[nv++].remainder_parallel = 0; }
        if (remainder_mode != 0) { variants[nv].threads = thread_list[i]; variants[nv++].remainder_parallel = 1; }
    }

    if (sd_coeff_load(coeff, &table)) goto done;
    entry = sd_coeff_find(&table, n, m, s, r);
    if (!entry) { fprintf(stderr, "No coefficients for n=%d m=%d s=%d r=%d\n", n, m, s, r); goto done; }
    if (gf_init(&gf, entry->w)) goto done;
    {
        sd_code_t code = { entry->n, entry->r, entry->m, entry->s, entry->w, entry->a };
        int cells = n * r, rows = sd_rows(&code);
        uint64_t hash_ops[MAX_VARIANTS] = { 0 };
        bytes = (size_t)mib * 1024 * 1024;
        sector = bytes / (size_t)cells / 64 * 64;
        bytes = sector * (size_t)cells;
        data_bytes = (size_t)(cells - rows) * sector;
        if (!sector || sd_build_H(&code, &gf, &H)) goto done;
        gold = malloc(bytes); work = malloc(bytes); is_parity = calloc((size_t)cells, 1);
        parity = malloc(sizeof *parity * (size_t)rows); faulty = malloc(sizeof *faulty * (size_t)rows);
        if (!gold || !work || !is_parity || !parity || !faulty) goto done;
        np = sd_parity_sectors(&code, parity);
        nf = sd_failure_sectors(&code, z, faulty);
        if (np != rows || nf != rows) { fprintf(stderr, "Invalid geometry (z=%d)\n", z); goto done; }
        for (i = 0; i < np; i++) is_parity[parity[i]] = 1;
        {
            uint32_t data_rng = 0x243f6a88U ^ (uint32_t)(n * 977 + r * 31 + m * 7 + s);
            size_t c, b;
            for (c = 0; c < (size_t)cells; c++)
                for (b = 0; b < sector; b++)
                    gold[c * sector + b] = is_parity[c] ? 0 : (uint8_t)random32(&data_rng);
        }
        if (ec_encode(&H, parity, np, gold, sector, &gf, &enc) ||
            !ec_syndrome_is_zero(&H, gold, sector, &gf)) goto done;

        out = fopen(csv, append ? "a" : "w");
        if (!out) { perror(csv); goto done; }
        if (!append || ftell(out) == 0)
            fprintf(out, "build,tag,implementation,n,m,s,r,w,z,stripe_mib,sector_bytes,"
                    "threads,remainder,effective_threads,trial,seconds,data_MiB_s,"
                    "mult_xors,used_partitioning,status\n");

        for (trial = -1; trial < trials; trial++) {   /* trial -1 = warm-up */
            int order[MAX_VARIANTS], pos;
            for (i = 0; i < nv; i++) order[i] = i;
            for (i = nv - 1; i > 0; i--) {
                int j = (int)(random32(&order_rng) % (uint32_t)(i + 1)), t = order[i];
                order[i] = order[j]; order[j] = t;
            }
            for (pos = 0; pos < nv; pos++) {
                const variant_t *v = &variants[order[pos]];
                int result, effective = 1, partitioned = 0;
                uint64_t ops;
                double start, elapsed;
                ppm_stats_t ps = { 0 };
                decode_stats_t ds = { 0 };
                memcpy(work, gold, bytes);
                for (i = 0; i < nf; i++) memset(work + (size_t)faulty[i] * sector, 0xa5, sector);
                start = seconds();
                if (v->threads == 0) {
                    result = ec_recover(&H, faulty, nf, work, sector, &gf, &ds);
                    ops = ds.mult_xors;
                } else {
#ifdef EVAL_M2
                    result = ppm_recover_sd(&code, &H, faulty, nf, work, sector, &gf, v->threads, &ps);
                    partitioned = ps.independent_groups > 0;
#else
                    result = ppm_recover_sd_config(&code, &H, faulty, nf, work, sector, &gf,
                                                   v->threads, v->remainder_parallel ? v->threads : 1, &ps);
                    partitioned = ps.used_partitioning;
#endif
                    ops = ps.mult_xors; effective = ps.effective_threads;
                }
                elapsed = seconds() - start;
                if (result || elapsed <= 0 || memcmp(work, gold, bytes) ||
                    !ec_syndrome_is_zero(&H, work, sector, &gf)) {
                    fprintf(stderr, "Validation failed: %s n=%d m=%d s=%d r=%d T=%d\n",
                            EVAL_BUILD, n, m, s, r, v->threads);
                    goto done;
                }
                if (trial == -1) { hash_ops[order[pos]] = ops; continue; }
                if (hash_ops[order[pos]] != ops) { fprintf(stderr, "Unstable op count\n"); goto done; }
                fprintf(out, "%s,%s,%s,%d,%d,%d,%d,%d,%d,%d,%zu,%d,%s,%d,%d,%.9f,%.3f,%" PRIu64 ",%d,PASS\n",
                        EVAL_BUILD, tag, v->threads ? "ppm" : "baseline", n, m, s, r, entry->w, z, mib,
                        sector, v->threads ? v->threads : 1,
                        v->remainder_parallel ? "parallel" : "serial", effective, trial + 1,
                        elapsed, data_bytes / (1024.0 * 1024.0) / elapsed, ops, partitioned);
            }
            fflush(out);
        }
        printf("PASS %s n=%d m=%d s=%d r=%d w=%d z=%d %d MiB: %d variants x %d trials\n",
               EVAL_BUILD, n, m, s, r, entry->w, z, mib, nv, trials);
    }
    rc = (out && ferror(out)) ? 2 : 0;
done:
    if (out && fclose(out)) rc = 2;
    free(gold); free(work); free(is_parity); free(parity); free(faulty);
    mat_free(&H); gf_free(&gf); sd_coeff_free(&table);
    return rc;
usage:
    fprintf(stderr, "Usage: %s --csv PATH [--append] [--trials N] [--stripe-mib MiB]\n"
            " [--n 4..24] [--r 4..24] [--m 1..3] [--s 1..3] [--z 1..s]\n"
            " [--threads LIST e.g. 1-12,16] [--remainder serial|parallel|both]\n"
            " [--baseline] [--tag TEXT] [--coefficients PATH]\n", argv[0]);
    goto done;
}

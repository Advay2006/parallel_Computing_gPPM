#define _POSIX_C_SOURCE 200809L

/* Final experiment: shared workload, warmed variants, shuffled trial order.
 * Decoder sources are reused without modifying the earlier checkpoints. */
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
#ifdef M4_WITH_CUDA
#include <cuda_profiler_api.h>
#include "gpu_backend.h"
#include "gpu_ppm.h"
#endif
#ifdef M4_CALLGRIND
#include <valgrind/callgrind.h>
#endif

typedef struct { const char *name; int threads; int gpu; } variant_t;
static const variant_t variants[] = {
    {"cpu_l3", 1, 0}, {"cpu_l3", 2, 0},
    {"cpu_l3", 4, 0}, {"cpu_l3", 8, 0},
#ifdef M4_WITH_CUDA
    {"gpu_scalar", 0, 1}, {"gpu_table", 0, 2},
#endif
};
#define NVAR ((int)(sizeof variants / sizeof variants[0]))

static uint32_t order_rng = 20261115U, data_rng = 0x243f6a88U;
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
static uint64_t checksum(const uint8_t *p, size_t n)
{
    uint64_t h = UINT64_C(1469598103934665603);
    size_t i;
    for (i = 0; i < n; i++) { h ^= p[i]; h *= UINT64_C(1099511628211); }
    return h;
}
static int number(const char *s)
{
    char *end; long n;
    errno = 0; n = strtol(s, &end, 10);
    if (errno || end == s || *end || n < 1 || n > 4096) return -1;
    return (int)n;
}
static int run_config(FILE *out, const sd_coeff_entry *entry, const gf_t *gf,
                      int mib, int trials, const int *selected, int nv,
                      int profile)
{
    sd_code_t code = { entry->n, entry->r, entry->m, entry->s, entry->w, entry->a };
    gf_mat H = { 0 };
    size_t bytes = (size_t)mib * 1024 * 1024, sector = bytes / 256;
    size_t data_bytes = (256 - (size_t)sd_rows(&code)) * sector;
    uint8_t *gold = NULL, *work = NULL, is_parity[256] = { 0 };
    int parity[80], faulty[80], np, nf, i, trial, rc = -1;
    uint64_t hash, expected[NVAR] = { 0 };
    decode_stats_t enc = { 0 };
    if (sector % (size_t)(code.w / 8) || sd_build_H(&code, gf, &H)) return -1;
    gold = calloc(1, bytes); work = malloc(bytes);
    if (!gold || !work) goto done;
    np = sd_parity_sectors(&code, parity); nf = sd_failure_sectors(&code, 1, faulty);
    if (np != sd_rows(&code) || nf != np) goto done;
    for (i = 0; i < np; i++) is_parity[parity[i]] = 1;
    for (i = 0; i < 256; i++) {
        size_t b;
        if (!is_parity[i]) for (b = 0; b < sector; b++)
            gold[(size_t)i * sector + b] = (uint8_t)random32(&data_rng);
    }
    if (ec_encode(&H, parity, np, gold, sector, gf, &enc) ||
        !ec_syndrome_is_zero(&H, gold, sector, gf)) goto done;
    hash = checksum(gold, bytes);
    for (trial = -1; trial < trials; trial++) {
        int order[NVAR], pos;
        for (i = 0; i < nv; i++) order[i] = selected[i];
        for (i = nv - 1; i > 0; i--) {
            int j = (int)(random32(&order_rng) % (uint32_t)(i + 1));
            int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
        }
        for (pos = 0; pos < nv; pos++) {
            int v = order[pos], result, effective = 0, partitioned;
            uint64_t ops, got_hash;
            double start, elapsed, c, kernel_sum = 0.0;
            ppm_stats_t cpu = { 0 };
#ifdef M4_WITH_CUDA
            gpu_ppm_stats_t gpu = { 0 };
#endif
            memcpy(work, gold, bytes);
            for (i = 0; i < nf; i++) memset(work + (size_t)faulty[i] * sector, 0xa5, sector);
            if (trial >= 0 && profile == 1) {
#ifdef M4_CALLGRIND
                CALLGRIND_START_INSTRUMENTATION;
                CALLGRIND_ZERO_STATS;
#endif
            }
#ifdef M4_WITH_CUDA
            if (trial >= 0 && profile == 2 && cudaProfilerStart() != cudaSuccess) goto done;
#endif
            start = seconds();
            if (!variants[v].gpu) {
                result = ppm_recover_sd(&code, &H, faulty, nf, work, sector,
                                        gf, variants[v].threads, &cpu);
                ops = cpu.mult_xors; c = cpu.c_multiply;
                effective = cpu.effective_threads; partitioned = cpu.used_partitioning;
            } else {
#ifdef M4_WITH_CUDA
                result = gpu_ppm_recover_sd(&code, &H, faulty, nf, work, sector,
                    gf, variants[v].gpu == 1 ? GPU_KERNEL_SCALAR : GPU_KERNEL_TABLE, &gpu);
                ops = gpu.mult_xors; c = gpu.c_multiply;
                kernel_sum = gpu.kernel_seconds_sum; partitioned = gpu.used_partitioning;
#else
                goto done;
#endif
            }
            elapsed = seconds() - start;
#ifdef M4_WITH_CUDA
            if (trial >= 0 && profile == 2 && cudaProfilerStop() != cudaSuccess) goto done;
#endif
            if (trial >= 0 && profile == 1) {
#ifdef M4_CALLGRIND
                CALLGRIND_DUMP_STATS;
                CALLGRIND_STOP_INSTRUMENTATION;
#endif
            }
            got_hash = checksum(work, bytes);
            if (result || elapsed <= 0 || memcmp(work, gold, bytes) || got_hash != hash ||
                !ec_syndrome_is_zero(&H, work, sector, gf)) {
                fprintf(stderr, "Validation failed: m=%d s=%d %s T=%d trial=%d\n",
                        code.m, code.s, variants[v].name, variants[v].threads, trial + 1);
                goto done;
            }
            if (trial == -1) { expected[v] = ops; continue; }
            if (expected[v] != ops) { fprintf(stderr, "Unstable operation count\n"); goto done; }
            if (fprintf(out, "%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.9f,%.3f,"
                        "%016" PRIx64 ",%" PRIu64 ",%.6f,%d,%.9f,%s,PASS\n",
                        variants[v].name, code.m, code.s, code.w, mib,
                        variants[v].threads, effective, trial + 1, pos + 1, nv,
                        elapsed, data_bytes / (1024.0 * 1024.0) / elapsed,
                        got_hash, ops, c, partitioned, kernel_sum,
                        profile ? "profiled" : "native") < 0) goto done;
        }
        fflush(out);
    }
    printf("PASS m=%d s=%d w=%d stripe=%d MiB: %d variants x %d trials\n",
           code.m, code.s, code.w, mib, nv, trials);
    rc = 0;
done:
    free(gold); free(work); mat_free(&H);
    return rc;
}

int main(int argc, char **argv)
{
    const char *csv = "results/milestone4/final_32mib.csv", *filter = "all";
    const char *coeff = "data/FAST-Coefficients.txt";
    int trials = 10, mib = 32, m_filter = 0, s_filter = 0, thread_filter = 0;
    int profile = 0, opt, selected[NVAR], nv = 0, m, s, i, rc = 2;
    FILE *out = NULL;
    sd_coeff_table table = { 0 };
    gf_t fields[3] = { { 0 }, { 0 }, { 0 } };
    static const struct option options[] = {
        {"csv", required_argument, NULL, 'o'}, {"trials", required_argument, NULL, 't'},
        {"stripe-mib", required_argument, NULL, 'b'}, {"m", required_argument, NULL, 'm'},
        {"s", required_argument, NULL, 's'}, {"threads", required_argument, NULL, 'j'},
        {"variant", required_argument, NULL, 'v'}, {"profile", required_argument, NULL, 'p'},
        {"coefficients", required_argument, NULL, 'c'}, {NULL, 0, NULL, 0}
    };
    while ((opt = getopt_long(argc, argv, "", options, NULL)) != -1) {
        switch (opt) {
        case 'o': csv = optarg; break;
        case 't': trials = number(optarg); if (trials < 1 || trials > 100) goto usage; break;
        case 'b': mib = number(optarg); if (mib < 1 || mib > 512) goto usage; break;
        case 'm': m_filter = number(optarg); if (m_filter < 1 || m_filter > 3) goto usage; break;
        case 's': s_filter = number(optarg); if (s_filter < 1 || s_filter > 3) goto usage; break;
        case 'j': thread_filter = number(optarg);
                  if (thread_filter != 1 && thread_filter != 2 && thread_filter != 4 && thread_filter != 8) goto usage;
                  break;
        case 'v': filter = optarg; break;
        case 'c': coeff = optarg; break;
        case 'p':
            if (!strcmp(optarg, "callgrind")) {
#ifdef M4_CALLGRIND
                profile = 1;
#else
                fprintf(stderr, "Use the m4_profile_cpu build for Callgrind.\n"); goto done;
#endif
            } else if (!strcmp(optarg, "cuda")) {
#ifdef M4_WITH_CUDA
                profile = 2;
#else
                goto usage;
#endif
            } else goto usage;
            break;
        default: goto usage;
        }
    }
    if (optind != argc) goto usage;
    for (i = 0; i < NVAR; i++)
        if ((!strcmp(filter, "all") || !strcmp(filter, variants[i].name) ||
             (!strcmp(filter, "cpu") && !variants[i].gpu)) &&
            (!thread_filter || variants[i].threads == thread_filter)) selected[nv++] = i;
    if (!nv || (profile && (nv != 1 || !m_filter || !s_filter || trials != 1)) ||
        (profile == 1 && variants[selected[0]].gpu) ||
        (profile == 2 && !variants[selected[0]].gpu)) goto usage;
    out = fopen(csv, "w");
    if (!out) { perror(csv); goto done; }
    if (sd_coeff_load(coeff, &table) || gf_init(&fields[0], 8) ||
        gf_init(&fields[1], 16) || gf_init(&fields[2], 32)) goto done;
    fprintf(out, "implementation,m,s,w,stripe_mib,threads,effective_threads,trial,"
            "order_position,variants,seconds,data_MiB_s,checksum,logical_mult_xors,"
            "c_multiply,used_partitioning,kernel_seconds_sum,measurement,status\n");
    for (m = 1; m <= 3; m++) for (s = 1; s <= 3; s++) {
        const sd_coeff_entry *entry;
        if ((m_filter && m != m_filter) || (s_filter && s != s_filter)) continue;
        entry = sd_coeff_find(&table, 16, m, s, 16);
        if (!entry || entry->w != (s == 1 ? 8 : s == 2 ? 16 : 32) ||
            run_config(out, entry, &fields[s - 1], mib, trials, selected, nv, profile)) goto done;
    }
    if (ferror(out)) goto done;
    rc = 0;
done:
    if (out && fclose(out)) rc = 2;
    for (i = 0; i < 3; i++) gf_free(&fields[i]);
    sd_coeff_free(&table);
    return rc;
usage:
    fprintf(stderr, "Usage: %s [--csv PATH] [--trials 1..100] [--stripe-mib 1..512]\n"
            " [--m 1..3] [--s 1..3] [--variant all|cpu|gpu_scalar|gpu_table]\n"
            " [--threads 1|2|4|8] [--profile callgrind|cuda] [--coefficients PATH]\n"
            " Profiling requires one variant, one configuration, and --trials 1.\n", argv[0]);
    goto done;
}

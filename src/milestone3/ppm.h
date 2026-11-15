/* ppm.h -- SD-specific Partitioned and Parallel Matrix recovery. */
#ifndef PPM_H
#define PPM_H

#include <stddef.h>
#include <stdint.h>

#include "gf.h"
#include "matrix.h"
#include "sd_code.h"

typedef struct {
    int requested_threads;
    int effective_threads;
    int remainder_threads;
    int independent_groups;
    int dependent_failures;
    int used_partitioning;
    double estimated_cost;
    double c_multiply;
    uint64_t independent_mult_xors;
    uint64_t remainder_mult_xors;
    uint64_t mult_xors;
} ppm_stats_t;

/* Recover using gPPM cost selection: choose normal or matrix-first for each
 * candidate matrix, then choose whole-matrix decoding or partitioned PPM. */
int ppm_recover_sd(const sd_code_t *code, const gf_mat *H,
                   const int *faulty, int nf,
                   uint8_t *stripe, size_t sector_bytes,
                   const gf_t *gf, int requested_threads,
                   ppm_stats_t *stats);

/* Variant used to measure whether parallelizing the dependent remainder helps.
 * remainder_threads=1 keeps the remainder serial while retaining parallel
 * recovery of independent groups. */
int ppm_recover_sd_config(const sd_code_t *code, const gf_mat *H,
                          const int *faulty, int nf,
                          uint8_t *stripe, size_t sector_bytes,
                          const gf_t *gf, int requested_threads,
                          int remainder_threads, ppm_stats_t *stats);

#define ppm_encode_sd(code, H, parity, np, stripe, sb, gf, threads, stats) \
        ppm_recover_sd((code), (H), (parity), (np), (stripe), (sb), \
                       (gf), (threads), (stats))

#endif /* PPM_H */

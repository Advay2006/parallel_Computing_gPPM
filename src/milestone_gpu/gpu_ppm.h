#ifndef GPU_PPM_H
#define GPU_PPM_H

#include <stddef.h>
#include <stdint.h>

#include "gf.h"
#include "matrix.h"
#include "sd_code.h"

typedef struct {
    int kernel_variant;
    int used_partitioning;
    int independent_groups;
    int dependent_failures;
    double c_multiply;
    double estimated_cost;
    double kernel_seconds_sum; /* sum of per-kernel event durations */
    uint64_t mult_xors;
} gpu_ppm_stats_t;

/* Cost-selected SD gPPM recovery on the GPU. kernel_variant selects the
 * bitwise scalar CUDA kernel or the table-accelerated CUDA kernel. */
int gpu_ppm_recover_sd(const sd_code_t *code, const gf_mat *H,
                       const int *faulty, int nf,
                       uint8_t *stripe, size_t sector_bytes,
                       const gf_t *gf, int kernel_variant,
                       gpu_ppm_stats_t *stats);

#endif

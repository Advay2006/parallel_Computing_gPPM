#ifndef GPU_BACKEND_H
#define GPU_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gpu_session gpu_session_t;

enum {
    GPU_KERNEL_SCALAR = 0,
    GPU_KERNEL_TABLE = 1
};

int gpu_session_create(gpu_session_t **out, const uint8_t *stripe,
                       size_t stripe_bytes, size_t temp_bytes, int width,
                       int kernel_variant, const uint32_t *logt,
                       size_t log_entries, const uint32_t *expt,
                       size_t exp_entries, const uint32_t *split8,
                       size_t split_entries);

int gpu_session_matvec(gpu_session_t *session, const uint32_t *matrix,
                       int rows, int cols, const int *source_sectors,
                       const int *destination_sectors, size_t sector_bytes,
                       int source_is_temp, int destination_is_temp,
                       int workspace_id);

int gpu_session_wait(gpu_session_t *session);

int gpu_session_copy_back(gpu_session_t *session, uint8_t *stripe);
double gpu_session_kernel_seconds_sum(const gpu_session_t *session);
void gpu_session_destroy(gpu_session_t *session);

/* GPU-specific estimate of the relative multiply cost in C = u + c*v. */
double gpu_calibrate_c(int width, int kernel_variant,
                       const uint32_t *logt, size_t log_entries,
                       const uint32_t *expt, size_t exp_entries,
                       const uint32_t *split8, size_t split_entries);

#ifdef __cplusplus
}
#endif

#endif

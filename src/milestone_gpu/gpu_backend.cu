#include "gpu_backend.h"

#include <cuda_runtime.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define GPU_SPLIT_PLANES 16U
#define GPU_SPLIT_SIZE 65536U
#define GPU_MAX_JOBS 64
#define GPU_MAX_JOB_ALLOCS 8
#define GPU_MAX_KERNELS_PER_JOB 2

struct gpu_workspace {
    cudaStream_t stream;
    int active;
    uint8_t *temp;
    cudaEvent_t kernel_start[GPU_MAX_KERNELS_PER_JOB];
    cudaEvent_t kernel_end[GPU_MAX_KERNELS_PER_JOB];
    int kernel_count;
    void *allocations[GPU_MAX_JOB_ALLOCS];
    int allocation_count;
};

struct gpu_session {
    uint8_t *d_stripe;
    uint32_t *d_logt;
    uint32_t *d_expt;
    uint32_t *d_split8;
    size_t stripe_bytes;
    size_t temp_bytes;
    int width;
    int kernel_variant;
    double kernel_seconds_sum;
    gpu_workspace workspaces[GPU_MAX_JOBS];
};

__device__ static uint32_t gpu_mul_scalar(uint32_t a, uint32_t b, int width)
{
    uint32_t result = 0;
    uint32_t low_poly = width == 8 ? 0x1dU :
                        width == 16 ? 0x100bU : 0x00400007U;
    int bit;

    for (bit = 0; bit < width; bit++) {
        uint32_t carry;
        if (b & 1U) result ^= a;
        b >>= 1;
        carry = a >> (width - 1);
        a = (a << 1) & (width == 32 ? 0xffffffffU : (1U << width) - 1U);
        if (carry) a ^= low_poly;
    }
    return result;
}

__device__ static uint32_t gpu_mul_table(uint32_t a, uint32_t b, int width,
                                         const uint32_t *logt,
                                         const uint32_t *expt,
                                         const uint32_t *split8)
{
    uint32_t product = 0;
    unsigned int ai, bj;

    if (a == 0 || b == 0) return 0;
    if (width != 32) return expt[logt[a] + logt[b]];

    for (ai = 0; ai < 4; ai++) {
        unsigned int av = (a >> (8 * ai)) & 0xffU;
        if (av == 0) continue;
        for (bj = 0; bj < 4; bj++) {
            unsigned int bv = (b >> (8 * bj)) & 0xffU;
            const uint32_t *plane;
            if (bv == 0) continue;
            plane = split8 + (size_t)(ai * 4 + bj) * GPU_SPLIT_SIZE;
            product ^= plane[(av << 8) | bv];
        }
    }
    return product;
}

__device__ static uint32_t gpu_mul(uint32_t a, uint32_t b, int width,
                                   int kernel_variant,
                                   const uint32_t *logt,
                                   const uint32_t *expt,
                                   const uint32_t *split8)
{
    if (a == 1) return b;
    return kernel_variant == GPU_KERNEL_TABLE
        ? gpu_mul_table(a, b, width, logt, expt, split8)
        : gpu_mul_scalar(a, b, width);
}

__device__ static uint32_t load_symbol(const uint8_t *buffer, size_t offset,
                                       int width)
{
    if (width == 8) return buffer[offset];
    if (width == 16)
        return ((const uint16_t *)(buffer + offset))[0];
    return ((const uint32_t *)(buffer + offset))[0];
}

__device__ static void store_symbol(uint8_t *buffer, size_t offset,
                                    int width, uint32_t value)
{
    if (width == 8)
        buffer[offset] = (uint8_t)value;
    else if (width == 16)
        ((uint16_t *)(buffer + offset))[0] = (uint16_t)value;
    else
        ((uint32_t *)(buffer + offset))[0] = value;
}

__global__ static void matrix_vector_kernel(
    const uint32_t *matrix, int rows, int cols,
    const int *source_sectors, const int *destination_sectors,
    const uint8_t *stripe, uint8_t *temp,
    size_t sector_bytes, size_t symbols, int width,
    int source_is_temp, int destination_is_temp, int kernel_variant,
    const uint32_t *logt, const uint32_t *expt, const uint32_t *split8)
{
    size_t output = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
    size_t outputs = (size_t)rows * symbols;
    int row;
    size_t symbol, byte_offset;
    const uint8_t *source = source_is_temp ? temp : stripe;
    uint8_t *destination = destination_is_temp ? temp : (uint8_t *)stripe;
    uint32_t accumulator = 0;
    int j;

    if (output >= outputs) return;
    row = (int)(output / symbols);
    symbol = output % symbols;
    byte_offset = symbol * (size_t)(width / 8);
    for (j = 0; j < cols; j++) {
        uint32_t coefficient = matrix[(size_t)row * cols + j];
        if (coefficient != 0) {
            int source_sector = source_is_temp ? j : source_sectors[j];
            size_t source_offset = (size_t)source_sector * sector_bytes
                                 + byte_offset;
            uint32_t value = load_symbol(source, source_offset, width);
            accumulator ^= gpu_mul(coefficient, value, width, kernel_variant,
                                   logt, expt, split8);
        }
    }

    int destination_sector = destination_is_temp ? row : destination_sectors[row];
    store_symbol(destination,
                 (size_t)destination_sector * sector_bytes + byte_offset,
                 width, accumulator);
}

__global__ static void init_region_kernel(uint8_t *src, uint8_t *dst,
                                          size_t symbols, int width)
{
    size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
    size_t bytes = (size_t)(width / 8);

    if (i < symbols) {
        uint32_t value = (uint32_t)(i * 37U + 11U);
        store_symbol(src, i * bytes, width, value);
        store_symbol(dst, i * bytes, width, (uint32_t)(i * 13U + 7U));
    }
}

__global__ static void probe_region_kernel(
    const uint8_t *src, uint8_t *dst, size_t symbols, int width,
    uint32_t coefficient, int kernel_variant,
    const uint32_t *logt, const uint32_t *expt, const uint32_t *split8)
{
    size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
    size_t bytes = (size_t)(width / 8);
    uint32_t value;

    if (i >= symbols) return;
    value = load_symbol(dst, i * bytes, width);
    uint32_t source = load_symbol(src, i * bytes, width);
    value ^= gpu_mul(coefficient, source, width, kernel_variant,
                     logt, expt, split8);
    store_symbol(dst, i * bytes, width, value);
}

static int copy_device_tables(gpu_session_t *session,
                              const uint32_t *logt, size_t log_entries,
                              const uint32_t *expt, size_t exp_entries,
                              const uint32_t *split8, size_t split_entries)
{
    if (session->width == 32) {
        if (!split8 || split_entries != GPU_SPLIT_PLANES * GPU_SPLIT_SIZE ||
            cudaMalloc(&session->d_split8, split_entries * sizeof(uint32_t)) != cudaSuccess ||
            cudaMemcpy(session->d_split8, split8,
                       split_entries * sizeof(uint32_t), cudaMemcpyHostToDevice) != cudaSuccess)
            return -1;
    } else {
        size_t order = (size_t)1U << session->width;
        size_t required_exponents = 2 * (order - 1) + 1;
        if (!logt || !expt || log_entries < order ||
            exp_entries < required_exponents ||
            cudaMalloc(&session->d_logt, log_entries * sizeof(uint32_t)) != cudaSuccess ||
            cudaMalloc(&session->d_expt, exp_entries * sizeof(uint32_t)) != cudaSuccess ||
            cudaMemcpy(session->d_logt, logt, log_entries * sizeof(uint32_t), cudaMemcpyHostToDevice) != cudaSuccess ||
            cudaMemcpy(session->d_expt, expt, exp_entries * sizeof(uint32_t), cudaMemcpyHostToDevice) != cudaSuccess)
            return -1;
    }
    return 0;
}

extern "C" int gpu_session_create(gpu_session_t **out, const uint8_t *stripe,
                                   size_t stripe_bytes, size_t temp_bytes,
                                   int width, int kernel_variant,
                                   const uint32_t *logt, size_t log_entries,
                                   const uint32_t *expt, size_t exp_entries,
                                   const uint32_t *split8, size_t split_entries)
{
    gpu_session_t *session;

    if (!out || !stripe || stripe_bytes == 0 || temp_bytes == 0 ||
        (width != 8 && width != 16 && width != 32) ||
        (kernel_variant != GPU_KERNEL_SCALAR && kernel_variant != GPU_KERNEL_TABLE))
        return -1;
    *out = nullptr;
    session = (gpu_session_t *)calloc(1, sizeof *session);
    if (!session) return -1;
    session->stripe_bytes = stripe_bytes;
    session->temp_bytes = temp_bytes;
    session->width = width;
    session->kernel_variant = kernel_variant;
    if (cudaMalloc(&session->d_stripe, stripe_bytes) != cudaSuccess ||
        cudaMemcpy(session->d_stripe, stripe, stripe_bytes,
                   cudaMemcpyHostToDevice) != cudaSuccess ||
        (kernel_variant == GPU_KERNEL_TABLE &&
         copy_device_tables(session, logt, log_entries, expt, exp_entries,
                            split8, split_entries) != 0)) {
        gpu_session_destroy(session);
        return -1;
    }
    *out = session;
    return 0;
}

extern "C" int gpu_session_matvec(gpu_session_t *session,
                                  const uint32_t *matrix, int rows, int cols,
                                  const int *source_sectors,
                                  const int *destination_sectors,
                                  size_t sector_bytes, int source_is_temp,
                                  int destination_is_temp, int workspace_id)
{
    uint32_t *d_matrix = nullptr;
    int *d_source = nullptr, *d_destination = nullptr;
    size_t symbols, outputs, blocks, matrix_entries;
    gpu_workspace *workspace;
    size_t stripe_sectors;
    int base_allocations;
    int event_index = -1;

    if (!session || !matrix || workspace_id < 0 || workspace_id >= GPU_MAX_JOBS ||
        (!source_is_temp && !source_sectors) ||
        (!destination_is_temp && !destination_sectors) ||
        rows <= 0 || cols <= 0 || sector_bytes == 0 ||
        sector_bytes % (size_t)(session->width / 8) != 0)
        return -1;
    if (((size_t)cols > session->temp_bytes / sector_bytes && source_is_temp) ||
        ((size_t)rows > session->temp_bytes / sector_bytes && destination_is_temp) ||
        (size_t)rows > SIZE_MAX / (size_t)cols)
        return -1;
    matrix_entries = (size_t)rows * (size_t)cols;
    if (matrix_entries > SIZE_MAX / sizeof(uint32_t) ||
        (!source_is_temp && (size_t)cols > SIZE_MAX / sizeof(int)) ||
        (!destination_is_temp && (size_t)rows > SIZE_MAX / sizeof(int)))
        return -1;
    if (session->stripe_bytes % sector_bytes != 0) return -1;
    stripe_sectors = session->stripe_bytes / sector_bytes;
    if (!source_is_temp) {
        int i;
        for (i = 0; i < cols; i++)
            if (source_sectors[i] < 0 ||
                (size_t)source_sectors[i] >= stripe_sectors)
                return -1;
    }
    if (!destination_is_temp) {
        int i;
        for (i = 0; i < rows; i++)
            if (destination_sectors[i] < 0 ||
                (size_t)destination_sectors[i] >= stripe_sectors)
                return -1;
    }
    symbols = sector_bytes / (size_t)(session->width / 8);
    if ((size_t)rows > SIZE_MAX / symbols) return -1;
    outputs = (size_t)rows * symbols;
    if (outputs > SIZE_MAX - 255U) return -1;
    blocks = (outputs + 255U) / 256U;
    if (blocks > UINT_MAX) return -1;
    workspace = &session->workspaces[workspace_id];
    if (!workspace->active) {
        if (cudaStreamCreateWithFlags(&workspace->stream, cudaStreamNonBlocking) != cudaSuccess)
            return -1;
        workspace->active = 1;
    }
    base_allocations = workspace->allocation_count;
    if (destination_is_temp && !workspace->temp &&
        cudaMallocAsync((void **)&workspace->temp, session->temp_bytes,
                        workspace->stream) != cudaSuccess)
        return -1;
    if (workspace->allocation_count + 3 > GPU_MAX_JOB_ALLOCS ||
        cudaMallocAsync((void **)&d_matrix,
                        matrix_entries * sizeof(uint32_t),
                        workspace->stream) != cudaSuccess)
        goto fail;
    workspace->allocations[workspace->allocation_count++] = d_matrix;
    if (!source_is_temp) {
        if (cudaMallocAsync((void **)&d_source, (size_t)cols * sizeof(int),
                            workspace->stream) != cudaSuccess)
            goto fail;
        workspace->allocations[workspace->allocation_count++] = d_source;
    }
    if (!destination_is_temp) {
        if (cudaMallocAsync((void **)&d_destination, (size_t)rows * sizeof(int),
                            workspace->stream) != cudaSuccess)
            goto fail;
        workspace->allocations[workspace->allocation_count++] = d_destination;
    }
    if (cudaMemcpyAsync(d_matrix, matrix,
                        matrix_entries * sizeof(uint32_t),
                        cudaMemcpyHostToDevice, workspace->stream) != cudaSuccess ||
        (!source_is_temp && cudaMemcpyAsync(
            d_source, source_sectors, (size_t)cols * sizeof(int),
            cudaMemcpyHostToDevice, workspace->stream) != cudaSuccess) ||
        (!destination_is_temp && cudaMemcpyAsync(
            d_destination, destination_sectors, (size_t)rows * sizeof(int),
            cudaMemcpyHostToDevice, workspace->stream) != cudaSuccess))
        goto fail;

    event_index = workspace->kernel_count;
    if (event_index >= GPU_MAX_KERNELS_PER_JOB ||
        cudaEventCreate(&workspace->kernel_start[event_index]) != cudaSuccess ||
        cudaEventCreate(&workspace->kernel_end[event_index]) != cudaSuccess ||
        cudaEventRecord(workspace->kernel_start[event_index],
                        workspace->stream) != cudaSuccess)
        goto fail;
    matrix_vector_kernel<<<(unsigned int)blocks, 256, 0, workspace->stream>>>(
        d_matrix, rows, cols, d_source, d_destination,
        session->d_stripe, workspace->temp, sector_bytes, symbols,
        session->width, source_is_temp, destination_is_temp,
        session->kernel_variant, session->d_logt, session->d_expt,
        session->d_split8);
    if (cudaGetLastError() != cudaSuccess ||
        cudaEventRecord(workspace->kernel_end[event_index],
                        workspace->stream) != cudaSuccess)
        goto fail;
    workspace->kernel_count++;
    return 0;

fail:
    cudaStreamSynchronize(workspace->stream);
    if (event_index >= 0 && event_index < GPU_MAX_KERNELS_PER_JOB &&
        event_index == workspace->kernel_count) {
        if (workspace->kernel_start[event_index])
            cudaEventDestroy(workspace->kernel_start[event_index]);
        if (workspace->kernel_end[event_index])
            cudaEventDestroy(workspace->kernel_end[event_index]);
        workspace->kernel_start[event_index] = nullptr;
        workspace->kernel_end[event_index] = nullptr;
    }
    while (workspace->allocation_count > base_allocations)
        cudaFree(workspace->allocations[--workspace->allocation_count]);
    return -1;
}

extern "C" int gpu_session_wait(gpu_session_t *session)
{
    int i;

    if (!session) return -1;
    for (i = 0; i < GPU_MAX_JOBS; i++)
        if (session->workspaces[i].active) {
            gpu_workspace *workspace = &session->workspaces[i];
            int event;
            if (cudaStreamSynchronize(workspace->stream) != cudaSuccess)
                return -1;
            for (event = 0; event < workspace->kernel_count; event++) {
                float elapsed_ms = 0.0f;
                if (cudaEventElapsedTime(&elapsed_ms,
                                         workspace->kernel_start[event],
                                         workspace->kernel_end[event]) != cudaSuccess)
                    return -1;
                session->kernel_seconds_sum += (double)elapsed_ms * 1e-3;
                cudaEventDestroy(workspace->kernel_start[event]);
                cudaEventDestroy(workspace->kernel_end[event]);
                workspace->kernel_start[event] = nullptr;
                workspace->kernel_end[event] = nullptr;
            }
            workspace->kernel_count = 0;
        }
    return 0;
}

extern "C" int gpu_session_copy_back(gpu_session_t *session, uint8_t *stripe)
{
    if (!session || !stripe || gpu_session_wait(session) != 0) return -1;
    return cudaMemcpy(stripe, session->d_stripe, session->stripe_bytes,
                      cudaMemcpyDeviceToHost) == cudaSuccess ? 0 : -1;
}

extern "C" double gpu_session_kernel_seconds_sum(const gpu_session_t *session)
{
    return session ? session->kernel_seconds_sum : 0.0;
}

extern "C" void gpu_session_destroy(gpu_session_t *session)
{
    if (!session) return;
    gpu_session_wait(session);
    for (int i = 0; i < GPU_MAX_JOBS; i++) {
        gpu_workspace *workspace = &session->workspaces[i];
        int j;
        for (j = 0; j < workspace->allocation_count; j++)
            cudaFree(workspace->allocations[j]);
        if (workspace->temp) cudaFree(workspace->temp);
        for (int event = 0; event < GPU_MAX_KERNELS_PER_JOB; event++) {
            if (workspace->kernel_start[event]) cudaEventDestroy(workspace->kernel_start[event]);
            if (workspace->kernel_end[event]) cudaEventDestroy(workspace->kernel_end[event]);
        }
        if (workspace->active) cudaStreamDestroy(workspace->stream);
    }
    if (session->d_stripe) cudaFree(session->d_stripe);
    if (session->d_logt) cudaFree(session->d_logt);
    if (session->d_expt) cudaFree(session->d_expt);
    if (session->d_split8) cudaFree(session->d_split8);
    free(session);
}

extern "C" double gpu_calibrate_c(int width, int kernel_variant,
                                  const uint32_t *logt, size_t log_entries,
                                  const uint32_t *expt, size_t exp_entries,
                                  const uint32_t *split8, size_t split_entries)
{
    const size_t symbols = 32768;
    const int rounds = 16;
    static double cache[3][2];
    static int ready[3][2];
    uint8_t *d_src = nullptr, *d_dst = nullptr;
    uint32_t *d_logt = nullptr, *d_expt = nullptr, *d_split8 = nullptr;
    cudaEvent_t start = nullptr, stop = nullptr;
    float xor_ms = 0.0f, multiply_ms = 0.0f;
    double c = 1.0;
    size_t bytes = symbols * (size_t)(width / 8);
    unsigned int blocks = (unsigned int)((symbols + 255U) / 256U);
    int width_index = width == 8 ? 0 : width == 16 ? 1 : 2;
    int mode_index = kernel_variant == GPU_KERNEL_TABLE ? 1 : 0;
    double samples[2][16];

    if (width != 8 && width != 16 && width != 32) return 1.0;
    if (ready[width_index][mode_index]) return cache[width_index][mode_index];

    if ((width != 8 && width != 16 && width != 32) ||
        cudaMalloc(&d_src, bytes) != cudaSuccess ||
        cudaMalloc(&d_dst, bytes) != cudaSuccess)
        goto done;
    if (kernel_variant == GPU_KERNEL_TABLE) {
        if (width == 32) {
            if (!split8 || split_entries != GPU_SPLIT_PLANES * GPU_SPLIT_SIZE ||
                cudaMalloc(&d_split8, split_entries * sizeof(uint32_t)) != cudaSuccess ||
                cudaMemcpy(d_split8, split8, split_entries * sizeof(uint32_t), cudaMemcpyHostToDevice) != cudaSuccess)
                goto done;
        } else {
            size_t order = (size_t)1U << width;
            size_t required_exponents = 2 * (order - 1) + 1;
            if (!logt || !expt || log_entries < order ||
                exp_entries < required_exponents ||
                cudaMalloc(&d_logt, log_entries * sizeof(uint32_t)) != cudaSuccess ||
                cudaMalloc(&d_expt, exp_entries * sizeof(uint32_t)) != cudaSuccess ||
                cudaMemcpy(d_logt, logt, log_entries * sizeof(uint32_t), cudaMemcpyHostToDevice) != cudaSuccess ||
                cudaMemcpy(d_expt, expt, exp_entries * sizeof(uint32_t), cudaMemcpyHostToDevice) != cudaSuccess)
                goto done;
        }
    }
    if (cudaEventCreate(&start) != cudaSuccess || cudaEventCreate(&stop) != cudaSuccess)
        goto done;
    init_region_kernel<<<blocks, 256>>>(d_src, d_dst, symbols, width);
    if (cudaGetLastError() != cudaSuccess || cudaDeviceSynchronize() != cudaSuccess)
        goto done;

    /* Separate launches prevent an even number of identical XORs from
     * cancelling inside one kernel. Warm both paths and interleave probes. */
    for (int round = -4; round < rounds; round++) {
        for (int mode = 0; mode < 2; mode++) {
            float elapsed;
            if (cudaEventRecord(start) != cudaSuccess) goto done;
            probe_region_kernel<<<blocks, 256>>>(d_src, d_dst, symbols, width,
                                                 mode + 1, kernel_variant,
                                                 d_logt, d_expt, d_split8);
            if (cudaGetLastError() != cudaSuccess ||
                cudaEventRecord(stop) != cudaSuccess ||
                cudaEventSynchronize(stop) != cudaSuccess ||
                cudaEventElapsedTime(&elapsed, start, stop) != cudaSuccess)
                goto done;
            if (round >= 0) samples[mode][round] = elapsed;
        }
    }
    for (int mode = 0; mode < 2; mode++)
        for (int i = 1; i < rounds; i++) {
            double value = samples[mode][i];
            int j = i;
            while (j > 0 && samples[mode][j - 1] > value) {
                samples[mode][j] = samples[mode][j - 1];
                j--;
            }
            samples[mode][j] = value;
        }
    xor_ms = (float)((samples[0][7] + samples[0][8]) / 2);
    multiply_ms = (float)((samples[1][7] + samples[1][8]) / 2);
    if (xor_ms <= 0.0f) goto done;
    c = (double)multiply_ms / xor_ms - 1.0;
    if (c < 0.0) c = 0.0;

done:
    if (start) cudaEventDestroy(start);
    if (stop) cudaEventDestroy(stop);
    if (d_src) cudaFree(d_src);
    if (d_dst) cudaFree(d_dst);
    if (d_logt) cudaFree(d_logt);
    if (d_expt) cudaFree(d_expt);
    if (d_split8) cudaFree(d_split8);
    cache[width_index][mode_index] = c;
    ready[width_index][mode_index] = 1;
    return c;
}

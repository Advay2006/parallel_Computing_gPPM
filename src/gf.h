/* gf.h -- Galois Field GF(2^w) arithmetic and the mult_XORs() primitive.
 *
 * Milestone 1, sequential baseline.  Deliberately scalar: no SIMD.  Intel SSE/
 * AVX acceleration of this layer is Milestone 3 work (paper Sec. 5.1).
 */
#ifndef GF_H
#define GF_H

#include <stddef.h>
#include <stdint.h>

/* w=8 and w=16 use log/antilog tables.  A full log table is impractical for
 * w=32, so that field uses a scalar 8x8 split table instead. */
typedef struct {
    int       w;      /* word size in bits                     */
    uint64_t  order;  /* 2^w                                   */
    uint32_t  nz;     /* 2^w - 1, the multiplicative order     */
    uint32_t *logt;   /* [order]    discrete log, logt[0] unused */
    uint32_t *expt;   /* [2*nz + 1] antilog, doubled to skip a modulo */
    uint32_t *split8; /* w=32: 16 tables of 256x256 products    */
} gf_t;

int  gf_init(gf_t *gf, int w);
void gf_free(gf_t *gf);

uint32_t gf_mul32(const gf_t *gf, uint32_t a, uint32_t b);

static inline uint32_t gf_mul(const gf_t *gf, uint32_t a, uint32_t b)
{
    if (a == 0 || b == 0) return 0;
    if (gf->w == 32) return gf_mul32(gf, a, b);
    return gf->expt[gf->logt[a] + gf->logt[b]];
}

uint32_t gf_inv(const gf_t *gf, uint32_t a);
uint32_t gf_div(const gf_t *gf, uint32_t a, uint32_t b);
uint32_t gf_pow(const gf_t *gf, uint32_t a, uint32_t e);

/* ---- mult_XORs() ------------------------------------------------------- */
/* Paper Sec. 2.3: multiply the region d0 by the w-bit constant a over
 * GF(2^w), then XOR the product into the region d1.  Every piece of region
 * arithmetic in this project funnels through here, because the Milestone 1
 * validation (Context.md 2B) counts calls to it and compares against C1.
 *
 * Two counting rules the callers must respect, or the count will not match C1:
 *   - never call with a == 0   (C1 counts NONZERO coefficients, u(M))
 *   - always call with a == 1  (C1 counts nonzeros, NOT non-ones; special
 *     casing a==1 to a plain XOR would put the measured count under C1, and
 *     would also hide the u/v distinction gPPM monetises in Milestone 3)
 */
void mult_XORs(const uint8_t *d0, uint8_t *d1, uint32_t a,
               size_t nbytes, const gf_t *gf);

/* Instrumentation.  Each thread owns its counter so parallel recovery does not
 * add synchronization to the region-arithmetic hot path. */
extern _Thread_local uint64_t gf_mult_xors_count;
static inline void     gf_count_reset(void) { gf_mult_xors_count = 0; }
static inline uint64_t gf_count_get(void)   { return gf_mult_xors_count; }

#endif /* GF_H */

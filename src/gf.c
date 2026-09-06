#include "gf.h"
#include <stdlib.h>
#include <assert.h>

uint64_t gf_mult_xors_count = 0;

/* Primitive polynomials (Plank's conventional choices). */
#define PRIM_POLY_8   0x11DU
#define PRIM_POLY_16  0x1100BU

int gf_init(gf_t *gf, int w)
{
    uint32_t prim, x, i;

    if (w == 8)       prim = PRIM_POLY_8;
    else if (w == 16) prim = PRIM_POLY_16;
    else              return -1;

    gf->w     = w;
    gf->order = 1U << w;
    gf->nz    = gf->order - 1U;
    gf->logt  = calloc(gf->order,      sizeof *gf->logt);
    gf->expt  = calloc(2 * gf->nz + 1, sizeof *gf->expt);
    if (!gf->logt || !gf->expt) { gf_free(gf); return -1; }

    /* Walk the powers of the generator x = 2. */
    x = 1;
    for (i = 0; i < gf->nz; i++) {
        gf->expt[i]  = x;
        gf->logt[x]  = i;
        x <<= 1;
        if (x & gf->order) x ^= prim;
    }
    /* Second copy so gf_mul can index logt[a]+logt[b] (max 2*nz) directly. */
    for (i = 0; i <= gf->nz; i++)
        gf->expt[gf->nz + i] = gf->expt[i];

    return 0;
}

void gf_free(gf_t *gf)
{
    free(gf->logt); free(gf->expt);
    gf->logt = NULL; gf->expt = NULL;
}

uint32_t gf_pow(const gf_t *gf, uint32_t a, uint32_t e)
{
    if (e == 0) return 1;
    if (a == 0) return 0;
    /* log(a^e) = e*log(a) mod nz */
    return gf->expt[((uint64_t)e * gf->logt[a]) % gf->nz];
}

void mult_XORs(const uint8_t *d0, uint8_t *d1, uint32_t a,
               size_t nbytes, const gf_t *gf)
{
    size_t i;

    assert(a != 0);          /* zero coefficients must be skipped by the caller */
    gf_mult_xors_count++;

    if (gf->w == 8) {
        for (i = 0; i < nbytes; i++)
            d1[i] ^= (uint8_t)gf_mul(gf, a, d0[i]);
    } else { /* w == 16 */
        const uint16_t *s = (const uint16_t *)d0;
        uint16_t       *t = (uint16_t *)d1;
        assert(nbytes % 2 == 0);
        for (i = 0; i < nbytes / 2; i++)
            t[i] ^= (uint16_t)gf_mul(gf, a, s[i]);
    }
}

#include "gf.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

/* Counting stays off the shared hot path during OpenMP recovery. */
_Thread_local uint64_t gf_mult_xors_count = 0;

/* Primitive polynomials (Plank's conventional choices). */
#define PRIM_POLY_8   0x11DU
#define PRIM_POLY_16  0x1100BU
/* GF-Complete's default w=32 polynomial, with the x^32 term implicit. */
#define PRIM_POLY_32  0x00400007U

#define SPLIT_PLANES  16U
#define SPLIT_SIZE    65536U

static uint32_t gf_mul32_shift(uint32_t a, uint32_t b)
{
    uint32_t product = 0;

    while (b != 0) {
        if (b & 1U) product ^= a;
        b >>= 1;
        if (a & 0x80000000U)
            a = (a << 1) ^ PRIM_POLY_32;
        else
            a <<= 1;
    }
    return product;
}

int gf_init(gf_t *gf, int w)
{
    uint32_t prim, x, i;

    memset(gf, 0, sizeof *gf);

    if (w == 8)       prim = PRIM_POLY_8;
    else if (w == 16) prim = PRIM_POLY_16;
    else if (w == 32) {
        unsigned int bi, bj, av, bv;
        size_t entries = (size_t)SPLIT_PLANES * SPLIT_SIZE;

        gf->w = 32;
        gf->order = UINT64_C(1) << 32;
        gf->nz = UINT32_MAX;
        gf->split8 = malloc(entries * sizeof *gf->split8);
        if (!gf->split8) return -1;

        for (bi = 0; bi < 4; bi++)
            for (bj = 0; bj < 4; bj++) {
                uint32_t *plane = gf->split8 + (size_t)(bi * 4 + bj) * SPLIT_SIZE;
                for (av = 0; av < 256; av++)
                    for (bv = 0; bv < 256; bv++)
                        plane[(av << 8) | bv] = gf_mul32_shift(
                            (uint32_t)av << (8 * bi),
                            (uint32_t)bv << (8 * bj));
            }
        return 0;
    } else {
        return -1;
    }

    gf->w     = w;
    gf->order = UINT64_C(1) << w;
    gf->nz    = (uint32_t)(gf->order - 1U);
    gf->logt  = calloc((size_t)gf->order, sizeof *gf->logt);
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
    free(gf->logt); free(gf->expt); free(gf->split8);
    memset(gf, 0, sizeof *gf);
}

uint32_t gf_mul32(const gf_t *gf, uint32_t a, uint32_t b)
{
    uint32_t product = 0;
    unsigned int bi, bj;

    if (a == 0 || b == 0) return 0;
    for (bi = 0; bi < 4; bi++) {
        unsigned int av = (a >> (8 * bi)) & 0xffU;
        if (av == 0) continue;
        for (bj = 0; bj < 4; bj++) {
            unsigned int bv = (b >> (8 * bj)) & 0xffU;
            const uint32_t *plane;
            if (bv == 0) continue;
            plane = gf->split8 + (size_t)(bi * 4 + bj) * SPLIT_SIZE;
            product ^= plane[(av << 8) | bv];
        }
    }
    return product;
}

uint32_t gf_inv(const gf_t *gf, uint32_t a)
{
    assert(a != 0);
    if (gf->w == 32) return gf_pow(gf, a, UINT32_MAX - 1U);
    return gf->expt[gf->nz - gf->logt[a]];
}

uint32_t gf_div(const gf_t *gf, uint32_t a, uint32_t b)
{
    assert(b != 0);
    if (a == 0) return 0;
    if (gf->w == 32) return gf_mul(gf, a, gf_inv(gf, b));
    return gf->expt[gf->logt[a] + gf->nz - gf->logt[b]];
}

uint32_t gf_pow(const gf_t *gf, uint32_t a, uint32_t e)
{
    uint32_t result;

    if (e == 0) return 1;
    if (a == 0) return 0;
    if (gf->w == 32) {
        result = 1;
        while (e != 0) {
            if (e & 1U) result = gf_mul(gf, result, a);
            e >>= 1;
            if (e != 0) a = gf_mul(gf, a, a);
        }
        return result;
    }
    /* log(a^e) = e*log(a) mod nz */
    return gf->expt[((uint64_t)e * gf->logt[a]) % gf->nz];
}

void mult_XORs(const uint8_t *d0, uint8_t *d1, uint32_t a,
               size_t nbytes, const gf_t *gf)
{
    size_t i;

    assert(a != 0);          /* zero coefficients must be skipped by the caller */
    gf_mult_xors_count++;

    if (a == 1) {
        for (i = 0; i < nbytes; i++) d1[i] ^= d0[i];
    } else if (gf->w == 8) {
        for (i = 0; i < nbytes; i++)
            d1[i] ^= (uint8_t)gf_mul(gf, a, d0[i]);
    } else if (gf->w == 16) {
        assert(nbytes % 2 == 0);
        for (i = 0; i < nbytes; i += 2) {
            uint16_t source, target;
            memcpy(&source, d0 + i, sizeof source);
            memcpy(&target, d1 + i, sizeof target);
            target ^= (uint16_t)gf_mul(gf, a, source);
            memcpy(d1 + i, &target, sizeof target);
        }
    } else {
        assert(gf->w == 32 && nbytes % 4 == 0);
        if (nbytes >= 4096) {
            uint32_t products[4][256];
            unsigned int byte, value;

            for (byte = 0; byte < 4; byte++)
                for (value = 0; value < 256; value++)
                    products[byte][value] = gf_mul32(
                        gf, a, (uint32_t)value << (8 * byte));

            for (i = 0; i < nbytes; i += 4) {
                uint32_t source, target, product;
                memcpy(&source, d0 + i, sizeof source);
                memcpy(&target, d1 + i, sizeof target);
                product = products[0][source & 0xffU]
                        ^ products[1][(source >> 8) & 0xffU]
                        ^ products[2][(source >> 16) & 0xffU]
                        ^ products[3][source >> 24];
                target ^= product;
                memcpy(d1 + i, &target, sizeof target);
            }
        } else {
            for (i = 0; i < nbytes; i += 4) {
                uint32_t source, target;
                memcpy(&source, d0 + i, sizeof source);
                memcpy(&target, d1 + i, sizeof target);
                target ^= gf_mul32(gf, a, source);
                memcpy(d1 + i, &target, sizeof target);
            }
        }
    }
}

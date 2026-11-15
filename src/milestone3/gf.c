#include "gf.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#if defined(__i386__) || defined(__x86_64__)
#include <immintrin.h>
#define GF_HAS_X86_SIMD 1
#else
#define GF_HAS_X86_SIMD 0
#endif
#if GF_HAS_X86_SIMD && (defined(__GNUC__) || defined(__clang__)) && \
    !defined(M3_DISABLE_SIMD)
#define GF_SIMD_BUILD 1
#else
#define GF_SIMD_BUILD 0
#endif

/* Counting stays off the shared hot path during OpenMP recovery. */
_Thread_local uint64_t gf_mult_xors_count = 0;

double gf_cost_c(const gf_t *gf)
{
    enum { BYTES = 256 * 1024, ROUNDS = 8 };
    static double cache[3];
    static int ready[3];
    uint8_t *src = NULL, *dst = NULL;
    clock_t start, xor_ticks, mul_ticks;
    size_t i;
    int width_index, round;
    double ratio;

    if (!gf || (gf->w != 8 && gf->w != 16 && gf->w != 32)) return 1.0;
    width_index = gf->w == 8 ? 0 : gf->w == 16 ? 1 : 2;
    if (ready[width_index]) return cache[width_index];

    src = malloc(BYTES);
    dst = malloc(BYTES);
    if (!src || !dst) goto fallback;
    for (i = 0; i < BYTES; i++) {
        src[i] = (uint8_t)(i * 37U + 11U);
        dst[i] = (uint8_t)(i * 13U + 7U);
    }

    gf_count_reset();
    start = clock();
    for (round = 0; round < ROUNDS; round++)
        mult_XORs(src, dst, 1, BYTES, gf);
    xor_ticks = clock() - start;

    gf_count_reset();
    start = clock();
    for (round = 0; round < ROUNDS; round++)
        mult_XORs(src, dst, 2, BYTES, gf);
    mul_ticks = clock() - start;
    free(src);
    free(dst);
    src = dst = NULL;
    if (xor_ticks <= 0 || mul_ticks <= 0) goto fallback;

    /* A multiply-XOR includes the XOR itself; subtract its measured cost to
     * estimate the extra multiply cost used as c in u + c*v. */
    ratio = (double)mul_ticks / (double)xor_ticks - 1.0;
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1000.0) goto fallback;
    cache[width_index] = ratio;
    ready[width_index] = 1;
    return ratio;

fallback:
    free(src);
    free(dst);
    cache[width_index] = 1.0;
    ready[width_index] = 1;
    return 1.0;
}

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

static void mult_XORs_scalar_impl(const uint8_t *d0, uint8_t *d1, uint32_t a,
                                  size_t nbytes, const gf_t *gf)
{
    size_t i;

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

#if GF_SIMD_BUILD
__attribute__((target("ssse3")))
static void xor_region_ssse3(const uint8_t *src, uint8_t *dst, size_t nbytes)
{
    size_t i = 0;

    for (; i + 16 <= nbytes; i += 16) {
        __m128i x = _mm_loadu_si128((const __m128i *)(src + i));
        __m128i y = _mm_loadu_si128((const __m128i *)(dst + i));
        _mm_storeu_si128((__m128i *)(dst + i), _mm_xor_si128(x, y));
    }
    for (; i < nbytes; i++) dst[i] ^= src[i];
}

__attribute__((target("ssse3")))
static void mult_xors_gf8_ssse3(const uint8_t *src, uint8_t *dst,
                                uint8_t coefficient, size_t nbytes,
                                const gf_t *gf)
{
    uint8_t lo[16], hi[16];
    __m128i lo_table, hi_table, mask = _mm_set1_epi8(0x0f);
    size_t i = 0;
    int j;

    for (j = 0; j < 16; j++) {
        lo[j] = (uint8_t)gf_mul(gf, coefficient, (uint32_t)j);
        hi[j] = (uint8_t)gf_mul(gf, coefficient, (uint32_t)(j << 4));
    }
    lo_table = _mm_loadu_si128((const __m128i *)lo);
    hi_table = _mm_loadu_si128((const __m128i *)hi);
    for (; i + 16 <= nbytes; i += 16) {
        __m128i x = _mm_loadu_si128((const __m128i *)(src + i));
        __m128i y = _mm_loadu_si128((const __m128i *)(dst + i));
        __m128i low = _mm_and_si128(x, mask);
        __m128i high = _mm_and_si128(_mm_srli_epi16(x, 4), mask);
        __m128i product = _mm_xor_si128(_mm_shuffle_epi8(lo_table, low),
                                       _mm_shuffle_epi8(hi_table, high));
        _mm_storeu_si128((__m128i *)(dst + i), _mm_xor_si128(y, product));
    }
    for (; i < nbytes; i++) dst[i] ^= (uint8_t)gf_mul(gf, coefficient, src[i]);
}

__attribute__((target("avx2")))
static void xor_region_avx2(const uint8_t *src, uint8_t *dst, size_t nbytes)
{
    size_t i = 0;

    for (; i + 32 <= nbytes; i += 32) {
        __m256i x = _mm256_loadu_si256((const __m256i *)(src + i));
        __m256i y = _mm256_loadu_si256((const __m256i *)(dst + i));
        _mm256_storeu_si256((__m256i *)(dst + i), _mm256_xor_si256(x, y));
    }
    for (; i < nbytes; i++) dst[i] ^= src[i];
}

__attribute__((target("avx2")))
static void mult_xors_gf16_avx2(const uint8_t *src, uint8_t *dst,
                                uint16_t coefficient, size_t nbytes,
                                const gf_t *gf)
{
    uint32_t products[2][256];
    unsigned int byte, value;
    size_t i = 0;

    for (byte = 0; byte < 2; byte++)
        for (value = 0; value < 256; value++)
            products[byte][value] = gf_mul(
                gf, coefficient, (uint32_t)value << (8 * byte));

    for (; i + 16 <= nbytes; i += 16) {
        __m128i packed_src = _mm_loadu_si128((const __m128i *)(src + i));
        __m256i words = _mm256_cvtepu16_epi32(packed_src);
        __m256i lo = _mm256_and_si256(words, _mm256_set1_epi32(0xff));
        __m256i hi = _mm256_and_si256(_mm256_srli_epi32(words, 8),
                                      _mm256_set1_epi32(0xff));
        __m256i product = _mm256_xor_si256(
            _mm256_i32gather_epi32((const int *)products[0], lo, 4),
            _mm256_i32gather_epi32((const int *)products[1], hi, 4));
        __m128i lower = _mm256_castsi256_si128(product);
        __m128i upper = _mm256_extracti128_si256(product, 1);
        __m128i packed_product = _mm_packus_epi32(lower, upper);
        __m128i target = _mm_loadu_si128((const __m128i *)(dst + i));
        _mm_storeu_si128((__m128i *)(dst + i),
                         _mm_xor_si128(target, packed_product));
    }
    for (; i < nbytes; i += 2) {
        uint16_t source, target;
        memcpy(&source, src + i, sizeof source);
        memcpy(&target, dst + i, sizeof target);
        target ^= (uint16_t)gf_mul(gf, coefficient, source);
        memcpy(dst + i, &target, sizeof target);
    }
}

__attribute__((target("avx2")))
static void mult_xors_gf32_avx2(const uint8_t *src, uint8_t *dst,
                                uint32_t coefficient, size_t nbytes,
                                const gf_t *gf)
{
    uint32_t products[4][256];
    unsigned int byte, value;
    size_t i = 0;

    for (byte = 0; byte < 4; byte++)
        for (value = 0; value < 256; value++)
            products[byte][value] = gf_mul32(
                gf, coefficient, (uint32_t)value << (8 * byte));

    for (; i + 32 <= nbytes; i += 32) {
        __m256i source = _mm256_loadu_si256((const __m256i *)(src + i));
        __m256i index0 = _mm256_and_si256(source, _mm256_set1_epi32(0xff));
        __m256i index1 = _mm256_and_si256(_mm256_srli_epi32(source, 8),
                                          _mm256_set1_epi32(0xff));
        __m256i index2 = _mm256_and_si256(_mm256_srli_epi32(source, 16),
                                          _mm256_set1_epi32(0xff));
        __m256i index3 = _mm256_srli_epi32(source, 24);
        __m256i product = _mm256_i32gather_epi32(
            (const int *)products[0], index0, 4);
        product = _mm256_xor_si256(product, _mm256_i32gather_epi32(
            (const int *)products[1], index1, 4));
        product = _mm256_xor_si256(product, _mm256_i32gather_epi32(
            (const int *)products[2], index2, 4));
        product = _mm256_xor_si256(product, _mm256_i32gather_epi32(
            (const int *)products[3], index3, 4));
        __m256i target = _mm256_loadu_si256((const __m256i *)(dst + i));
        _mm256_storeu_si256((__m256i *)(dst + i),
                            _mm256_xor_si256(target, product));
    }
    for (; i < nbytes; i += 4) {
        uint32_t source, target;
        memcpy(&source, src + i, sizeof source);
        memcpy(&target, dst + i, sizeof target);
        target ^= gf_mul32(gf, coefficient, source);
        memcpy(dst + i, &target, sizeof target);
    }
}
#endif

void mult_XORs_scalar(const uint8_t *d0, uint8_t *d1, uint32_t a,
                      size_t nbytes, const gf_t *gf)
{
    assert(a != 0);
    gf_mult_xors_count++;
    mult_XORs_scalar_impl(d0, d1, a, nbytes, gf);
}

void mult_XORs(const uint8_t *d0, uint8_t *d1, uint32_t a,
               size_t nbytes, const gf_t *gf)
{
    assert(a != 0);          /* zero coefficients must be skipped by the caller */
    gf_mult_xors_count++;
#if GF_SIMD_BUILD
    if (gf->w == 8 && __builtin_cpu_supports("ssse3")) {
        if (a == 1)
            xor_region_ssse3(d0, d1, nbytes);
        else
            mult_xors_gf8_ssse3(d0, d1, (uint8_t)a, nbytes, gf);
        return;
    }
    if ((gf->w == 16 || gf->w == 32) && __builtin_cpu_supports("avx2")) {
        if (a == 1)
            xor_region_avx2(d0, d1, nbytes);
        else if (gf->w == 16)
            mult_xors_gf16_avx2(d0, d1, (uint16_t)a, nbytes, gf);
        else
            mult_xors_gf32_avx2(d0, d1, a, nbytes, gf);
        return;
    }
#endif
    mult_XORs_scalar_impl(d0, d1, a, nbytes, gf);
}

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gf.h"

#define GF32_POLY 0x00400007U

static uint32_t mul32_reference(uint32_t a, uint32_t b)
{
    uint32_t product = 0;

    while (b != 0) {
        if (b & 1U) product ^= a;
        b >>= 1;
        a = (a & 0x80000000U) ? (a << 1) ^ GF32_POLY : a << 1;
    }
    return product;
}

static int check_field(int w)
{
    gf_t gf;
    uint32_t state = 0x9e3779b9U;
    int i;

    if (gf_init(&gf, w) != 0) {
        fprintf(stderr, "gf_init(%d) failed\n", w);
        return 0;
    }

    for (i = 0; i < 1000; i++) {
        uint32_t a, b, product;
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        a = state;
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        b = state;
        if (w == 8) { a &= 0xffU; b &= 0xffU; }
        if (w == 16) { a &= 0xffffU; b &= 0xffffU; }

        product = gf_mul(&gf, a, b);
        if (w == 32 && product != mul32_reference(a, b)) {
            fprintf(stderr, "GF(2^32) multiply mismatch for %u * %u\n", a, b);
            gf_free(&gf);
            return 0;
        }
        if (a != 0 && gf_mul(&gf, a, gf_inv(&gf, a)) != 1) {
            fprintf(stderr, "GF(2^%d) inverse mismatch for %u\n", w, a);
            gf_free(&gf);
            return 0;
        }
        if (b != 0 && gf_div(&gf, product, b) != a) {
            fprintf(stderr, "GF(2^%d) division mismatch\n", w);
            gf_free(&gf);
            return 0;
        }
    }

    gf_free(&gf);
    return 1;
}

static int check_region32(void)
{
    gf_t gf;
    uint32_t source[2048], actual[2048], expected[2048];
    const uint32_t coefficient = 0x81234567U;
    size_t i;

    if (gf_init(&gf, 32) != 0) return 0;
    for (i = 0; i < 2048; i++) {
        source[i] = (uint32_t)i * 0x1020305U + 7U;
        actual[i] = expected[i] = (uint32_t)i ^ 0xa5a5a5a5U;
        expected[i] ^= mul32_reference(coefficient, source[i]);
    }

    gf_count_reset();
    mult_XORs((const uint8_t *)source, (uint8_t *)actual, coefficient,
              sizeof source, &gf);
    if (gf_count_get() != 1 || memcmp(actual, expected, sizeof actual) != 0) {
        fprintf(stderr, "GF(2^32) region multiply mismatch\n");
        gf_free(&gf);
        return 0;
    }

    mult_XORs((const uint8_t *)source, (uint8_t *)actual, 1, sizeof source, &gf);
    if (gf_count_get() != 2) {
        fprintf(stderr, "coefficient-one call was not counted\n");
        gf_free(&gf);
        return 0;
    }

    gf_free(&gf);
    return 1;
}

int main(void)
{
    if (!check_field(8) || !check_field(16) || !check_field(32) || !check_region32())
        return 1;
    printf("GF arithmetic tests: PASS\n");
    return 0;
}

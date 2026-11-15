#include "sd_code.h"
#include <stdlib.h>

int sd_build_H(const sd_code_t *code, const gf_t *gf, gf_mat *H)
{
    const int n = code->n, r = code->r, m = code->m, s = code->s;
    int i, j, l, c;

    if (mat_alloc(H, sd_rows(code), sd_cols(code)) != 0) return -1;

    /* Disk parity: m equations per stripe row, each confined to that row. */
    for (i = 0; i < r; i++)
        for (l = 0; l < m; l++)
            for (j = 0; j < n; j++)
                MAT(H, m * i + l, i * n + j) =
                    gf_pow(gf, code->a[l], (uint32_t)(i * n + j));

    /* Sector parity: s equations spanning every sector in the stripe. */
    for (l = 0; l < s; l++)
        for (c = 0; c < n * r; c++)
            MAT(H, m * r + l, c) = gf_pow(gf, code->a[m + l], (uint32_t)c);

    return 0;
}

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

int sd_parity_sectors(const sd_code_t *code, int *out)
{
    const int n = code->n, r = code->r, m = code->m, s = code->s;
    int i, j, c, k = 0, extra = 0;

    if (n <= m || r <= 0 || s > (n - m) * r) return -1;

    for (i = 0; i < r; i++)                       /* the m coding disks */
        for (j = n - m; j < n; j++)
            out[k++] = i * n + j;
    for (c = n * r - 1; c >= 0 && extra < s; c--) /* highest non-coding sectors */
        if (c % n < n - m) {
            out[k++] = c;
            extra++;
        }

    qsort(out, (size_t)k, sizeof *out, cmp_int);
    return k;
}

int sd_failure_sectors(const sd_code_t *code, int z, int *out)
{
    const int n = code->n, r = code->r, m = code->m, s = code->s;
    int i, j, row, base, rem, count, k = 0;

    if (z < 1 || z > s || z > r || s > z * (n - m)) return -1;

    for (i = 0; i < r; i++)
        for (j = 0; j < m; j++)
            out[k++] = i * n + j;

    base = s / z;
    rem = s % z;
    for (row = 0; row < z; row++) {
        count = base + (row < rem ? 1 : 0);
        for (j = 0; j < count; j++) out[k++] = row * n + m + j;
    }

    qsort(out, (size_t)k, sizeof *out, cmp_int);
    return k;
}

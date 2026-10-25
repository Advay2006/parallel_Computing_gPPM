#ifndef COEFFICIENTS_H
#define COEFFICIENTS_H

#include <stddef.h>
#include <stdint.h>

#define SD_COEFF_CONFIGS 3969
#define SD_MAX_COEFF 6

typedef struct {
    int n, m, s, r, w;
    uint32_t a[SD_MAX_COEFF];
} sd_coeff_entry;

typedef struct {
    sd_coeff_entry *entries;
    size_t count;
} sd_coeff_table;

/* Load and validate the complete 4<=n,r<=24, 1<=m,s<=3 FAST table. */
int sd_coeff_load(const char *path, sd_coeff_table *table);
void sd_coeff_free(sd_coeff_table *table);
const sd_coeff_entry *sd_coeff_find(const sd_coeff_table *table,
                                    int n, int m, int s, int r);

#endif /* COEFFICIENTS_H */

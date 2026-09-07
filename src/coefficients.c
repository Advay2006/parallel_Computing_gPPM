#include "coefficients.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int config_index(int n, int m, int s, int r)
{
    if (n < 4 || n > 24 || m < 1 || m > 3 ||
        s < 1 || s > 3 || r < 4 || r > 24)
        return -1;
    return (((n - 4) * 3 + (m - 1)) * 3 + (s - 1)) * 21 + (r - 4);
}

static int read_number(char **cursor, uint64_t *value)
{
    char *start = *cursor, *end;
    unsigned long long parsed;

    while (isspace((unsigned char)*start)) start++;
    if (*start == '\0' || *start == '-') return -1;
    errno = 0;
    parsed = strtoull(start, &end, 10);
    if (errno != 0 || end == start) return -1;
    *cursor = end;
    *value = (uint64_t)parsed;
    return 0;
}

void sd_coeff_free(sd_coeff_table *table)
{
    free(table->entries);
    table->entries = NULL;
    table->count = 0;
}

int sd_coeff_load(const char *path, sd_coeff_table *table)
{
    FILE *fp;
    char line[512];
    unsigned char *present = NULL;
    size_t line_number = 0, count = 0;
    int rc = -1;

    memset(table, 0, sizeof *table);
    fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "cannot open coefficient table %s\n", path);
        return -1;
    }
    table->entries = calloc(SD_COEFF_CONFIGS, sizeof *table->entries);
    present = calloc(SD_COEFF_CONFIGS, 1);
    if (!table->entries || !present) {
        fprintf(stderr, "cannot allocate coefficient table\n");
        goto done;
    }

    while (fgets(line, sizeof line, fp)) {
        uint64_t values[5], coefficient;
        sd_coeff_entry *entry;
        char *cursor = line;
        int index, i, nc;

        line_number++;
        while (isspace((unsigned char)*cursor)) cursor++;
        if (*cursor == '\0' || *cursor == '#') continue;

        for (i = 0; i < 5; i++)
            if (read_number(&cursor, &values[i]) != 0 || values[i] > UINT32_MAX) {
                fprintf(stderr, "%s:%zu: malformed configuration\n", path, line_number);
                goto done;
            }

        index = config_index((int)values[0], (int)values[1],
                             (int)values[2], (int)values[3]);
        if (index < 0 || (values[4] != 8 && values[4] != 16 && values[4] != 32)) {
            fprintf(stderr, "%s:%zu: configuration outside the supported grid\n",
                    path, line_number);
            goto done;
        }
        if (present[index]) {
            fprintf(stderr, "%s:%zu: duplicate configuration\n", path, line_number);
            goto done;
        }

        entry = &table->entries[index];
        entry->n = (int)values[0]; entry->m = (int)values[1];
        entry->s = (int)values[2]; entry->r = (int)values[3];
        entry->w = (int)values[4];
        nc = entry->m + entry->s;
        for (i = 0; i < nc; i++) {
            uint64_t limit = entry->w == 32 ? UINT32_MAX : (UINT64_C(1) << entry->w) - 1;
            if (read_number(&cursor, &coefficient) != 0 ||
                coefficient == 0 || coefficient > limit) {
                fprintf(stderr, "%s:%zu: invalid coefficient %d\n", path, line_number, i);
                goto done;
            }
            entry->a[i] = (uint32_t)coefficient;
        }
        while (isspace((unsigned char)*cursor)) cursor++;
        if (*cursor != '\0') {
            fprintf(stderr, "%s:%zu: trailing data after coefficients\n", path, line_number);
            goto done;
        }
        present[index] = 1;
        count++;
    }
    if (ferror(fp)) {
        fprintf(stderr, "error reading coefficient table %s\n", path);
        goto done;
    }
    if (count != SD_COEFF_CONFIGS) {
        fprintf(stderr, "%s: expected %d configurations, found %zu\n",
                path, SD_COEFF_CONFIGS, count);
        goto done;
    }

    table->count = count;
    rc = 0;

done:
    fclose(fp);
    free(present);
    if (rc != 0) sd_coeff_free(table);
    return rc;
}

const sd_coeff_entry *sd_coeff_find(const sd_coeff_table *table,
                                    int n, int m, int s, int r)
{
    int index = config_index(n, m, s, r);
    if (index < 0 || (size_t)index >= table->count) return NULL;
    return &table->entries[index];
}

#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "coefficients.h"

static int expect_rejected(const char *contents)
{
    char path[] = "/tmp/sd-coefficients-XXXXXX";
    sd_coeff_table table = { 0 };
    int fd = mkstemp(path), saved_stderr, null_stderr, rc;
    FILE *file;

    if (fd < 0) return 0;
    file = fdopen(fd, "w");
    if (!file) {
        close(fd);
        unlink(path);
        return 0;
    }
    if (fputs(contents, file) == EOF) {
        fclose(file);
        unlink(path);
        return 0;
    }
    if (fclose(file) != 0) {
        unlink(path);
        return 0;
    }

    fflush(stderr);
    saved_stderr = dup(STDERR_FILENO);
    null_stderr = open("/dev/null", O_WRONLY);
    if (saved_stderr < 0 || null_stderr < 0 ||
        dup2(null_stderr, STDERR_FILENO) < 0) {
        if (saved_stderr >= 0) close(saved_stderr);
        if (null_stderr >= 0) close(null_stderr);
        unlink(path);
        return 0;
    }
    close(null_stderr);
    rc = sd_coeff_load(path, &table);
    fflush(stderr);
    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stderr);
    unlink(path);
    sd_coeff_free(&table);
    return rc != 0;
}

int main(void)
{
    sd_coeff_table table = { 0 };
    const sd_coeff_entry *entry;
    int s;

    if (sd_coeff_load("data/FAST-Coefficients.txt", &table) != 0 ||
        table.count != SD_COEFF_CONFIGS) {
        fprintf(stderr, "valid coefficient table was rejected\n");
        return 1;
    }
    for (s = 1; s <= 3; s++) {
        entry = sd_coeff_find(&table, 16, 2, s, 16);
        if (!entry || entry->w != (s == 1 ? 8 : s == 2 ? 16 : 32)) {
            fprintf(stderr, "coefficient lookup mismatch\n");
            sd_coeff_free(&table);
            return 1;
        }
    }
    if (sd_coeff_find(&table, 3, 1, 1, 4) != NULL) {
        fprintf(stderr, "out-of-grid lookup was accepted\n");
        sd_coeff_free(&table);
        return 1;
    }
    sd_coeff_free(&table);

    if (!expect_rejected("4 1 1 4 8 1 2 trailing\n") ||
        !expect_rejected("4 1 1 4 8 1 2\n4 1 1 4 8 1 2\n") ||
        !expect_rejected("4 1 1 4 8 1 256\n") ||
        !expect_rejected("4 1 1 4 8 1 2\n")) {
        fprintf(stderr, "malformed coefficient table was accepted\n");
        return 1;
    }

    printf("Coefficient table tests: PASS\n");
    return 0;
}

/* Linker wrapper used only in the Callgrind build. Freeze the native cost
 * weight so emulation overhead cannot change the decoder's chosen schedule. */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "gf.h"

double __wrap_gf_cost_c(const gf_t *gf)
{
    const char *text = getenv("M4_CALIBRATION_C");
    char *end;
    double c;
    (void)gf;
    if (!text) {
        fprintf(stderr, "Set M4_CALIBRATION_C from the native benchmark before profiling.\n");
        exit(2);
    }
    errno = 0;
    c = strtod(text, &end);
    if (errno || end == text || *end || !isfinite(c) || c < 0 || c > 1000) {
        fprintf(stderr, "Invalid M4_CALIBRATION_C\n");
        exit(2);
    }
    return c;
}

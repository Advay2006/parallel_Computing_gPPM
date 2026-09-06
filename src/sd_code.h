/* sd_code.h -- Step 1 of paper Sec. 2.2: derive the parity-check matrix H.
 *
 * SD^{m,s}_{n,r}(w | a_0, ..., a_{m+s-1})
 *   n disks per stripe, r sectors per strip, m coding disks, s extra coding
 *   sectors, arithmetic over GF(2^w), m+s coding coefficients.
 */
#ifndef SD_CODE_H
#define SD_CODE_H

#include <stdint.h>
#include "gf.h"
#include "matrix.h"

typedef struct {
    int             n, r, m, s, w;
    const uint32_t *a;      /* m + s coefficients */
} sd_code_t;

static inline int sd_rows(const sd_code_t *c) { return c->m * c->r + c->s; } /* R_H */
static inline int sd_cols(const sd_code_t *c) { return c->n * c->r; }        /* C_H */

/* Step 1.  H is R_H x C_H; column i*n+j is sector b_{i*n+j} at stripe row i,
 * disk j.  Row layout (fixed by Algorithm 1, which addresses rows
 * m*i .. m*i+m-1 as the block belonging to stripe row i):
 *
 *   rows m*i + l,  0<=i<r, 0<=l<m   disk parity for stripe row i,
 *                                   H(row, i*n+j) = a_l^j, zero elsewhere.
 *                                   Row-local: n nonzeros per row.
 *   rows m*r + l,  0<=l<s           sector parity, spanning the whole stripe,
 *                                   H(row, c) = a_{m+l}^c.
 *                                   Dense: n*r nonzeros per row.
 *
 * That sparse/dense asymmetry is what makes SD an *asymmetric parity* code and
 * is the structural fact PPM exploits in Milestone 2.
 */
int sd_build_H(const sd_code_t *code, const gf_t *gf, gf_mat *H);

/* Sector indices of the m*r + s coding sectors, ascending.
 * Convention: the m rightmost disks are the coding disks, and the s extra
 * coding sectors sit in the last stripe row, working leftwards from the
 * coding disks.  Requires s <= n - m.  'out' must hold m*r + s ints. */
int sd_parity_sectors(const sd_code_t *code, int *out);

#endif /* SD_CODE_H */

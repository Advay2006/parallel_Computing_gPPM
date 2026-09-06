/* codec.h -- Steps 2-4 of paper Sec. 2.2: the traditional (baseline)
 * encode/decode.  Normal sequence only: S*BS first, then F^-1*(S*BS).
 * The matrix_first sequence is deliberately absent -- it belongs to gPPM in
 * Milestone 3, and implementing it here would blur the C1 baseline.
 */
#ifndef CODEC_H
#define CODEC_H

#include <stdint.h>
#include <stddef.h>
#include "gf.h"
#include "matrix.h"

typedef struct {
    long     u_Finv;      /* u(F^-1) */
    long     u_S;         /* u(S)    */
    uint64_t mult_xors;   /* measured calls; must equal u_Finv + u_S = C1 */
} decode_stats_t;

/* Step 2.  Split H by column into F (faulty columns) and S (surviving ones).
 * 'faulty' must be ascending and distinct; 'surviving' receives the surviving
 * column indices, ascending, and must hold H->cols - nf ints. */
int ec_split(const gf_mat *H, const int *faulty, int nf,
             gf_mat *F, gf_mat *S, int *surviving);

/* Step 4.  BF = Finv * (S * BS), normal sequence.  Reads the surviving sectors
 * of 'stripe' and overwrites the faulty ones with the recovered contents. */
void ec_decode_normal(const gf_mat *Finv, const gf_mat *S,
                      const int *faulty, const int *surviving,
                      uint8_t *stripe, size_t sector_bytes, const gf_t *gf);

/* Steps 2-4 together.  Returns 0, or -1 if F is singular for this failure
 * pattern (the pattern lies outside the code's correctable set).
 * Resets the mult_XORs counter, so stats->mult_xors covers Step 4 only --
 * the Step 3 inversion is excluded, per the paper's footnote 2. */
int ec_recover(const gf_mat *H, const int *faulty, int nf,
               uint8_t *stripe, size_t sector_bytes, const gf_t *gf,
               decode_stats_t *stats);

/* Encoding is decoding with the faulty set = the parity sector set
 * (paper footnote 1).  Same code path. */
#define ec_encode(H, parity, np, stripe, sb, gf, st) \
        ec_recover((H), (parity), (np), (stripe), (sb), (gf), (st))

/* Validation 2A: the invariant H*B = 0 over the whole stripe.
 * Returns 1 if every syndrome sector is all-zero, 0 otherwise. */
int ec_syndrome_is_zero(const gf_mat *H, const uint8_t *stripe,
                        size_t sector_bytes, const gf_t *gf);

#endif /* CODEC_H */

[Milestone 1] Literature Review & Sequential Baseline Implementation [6 Marks]
• Establish target problem metrics, write a reference C/Python single-threaded
model.
[Milestone 2] Initial Parallelization & Scaling Baseline [ 7 Marks]
• Write first parallel implementation (e.g., basic OpenMP or CUDA naive kernel).
[Milestone 3] Advanced Optimizations & Distributed Scaling [6 Marks]
• Add communication hiding, memory-tiling, vectorization, or MPI hybrid scaling.
[Milestone 4] Profiling, Benchmarking & Writing Research Paper [ 6 Marks]
• Profile performance via Nsight/Vune/TAU; publish final report and code repository




Following are expected Zipped Submissions at Each Milestone

Milestone 1 – Literature Review & Sequential Baseline [6 Marks]
Upload:
Problem definition, Literature review with proper references, Baseline results and correctness validation in PDF format
Sequential C/Python implementation


Milestone 2 – Initial Parallelization & Scaling Baseline [7 Marks]
Upload:
Parallel implementation (OpenMP/CUDA/MPI, etc.)
Correctness comparison with sequential version, Initial performance and scaling results in a PDF

Milestone 3 – Advanced Optimizations & Distributed Scaling [6 Marks]
Upload:
Optimized implementation
Description of optimization techniques used, Performance/scaling comparison with previous milestone in IEEE paper format PDF

Milestone 4 – Profiling, Benchmarking & Research Paper [6 Marks]
Upload:
Add Profiling results and bottleneck analysis, Final benchmark/scaling results in the Final research paper pdf
ZIP of the final code repository


The paper we ahve chosen: "gPPM: A Generalized Matrix Operation and Parallel
Algorithm to Accelerate the Encoding/Decoding Process of
Erasure Codes" 

path to pdf: /home/advay/Documents/Code/PC project/pc project paper.pdf



---

# MILESTONE 1 PLAN — Literature Review & Sequential Baseline

## 1. Sequential implementation

Implement the traditional (baseline) encode/decode exactly as the four steps of
paper Section 2.2. This is the reference model everything later is measured
against, so it stays deliberately naive: single-threaded, no SIMD, no
partitioning, normal sequence only.

Code parameterization (paper's notation): SD^{m,s}_{n,r}(w | a_0, ..., a_{m+s-1})
  n = disks in stripe, m = coding disks, s = extra coding sectors,
  r = rows (sectors) per strip, w = word size for GF(2^w).

Step 1 — Derive the parity-check matrix H directly from the code definition.
  H is R_H x C_H with R_H = m*r + s and C_H = n*r.
  Column i*n + j of H corresponds to sector b_{i*n+j} at stripe row i, disk j
  (0 <= i < r, 0 <= j < n).
  Structure: the first m*r rows are the per-row disk-parity (GRS) constraints,
  each touching only the n sectors of one stripe row; the last s rows are the
  sector-parity constraints, which touch sectors across the whole stripe.

Step 2 — Derive F and S from H.
  F = the columns of H corresponding to faulty blocks.
  S = the remaining columns (surviving blocks).
  Note F is square: number of faulty blocks = m*r + s = R_H in the worst case
  we test (m whole faulty disks + s additional faulty sectors).

Step 3 — Invert F to get F^-1 (Gauss-Jordan over GF(2^w)).

Step 4 — Recover B_F = F^-1 * S * B_S, using the NORMAL sequence:
  first T = S * B_S, then B_F = F^-1 * T.
  (matrix_first is intentionally NOT implemented in M1 — it belongs to M3/gPPM.)

Supporting layer:
  - GF(2^w) arithmetic: log/antilog tables for w = 8 and w = 16; w = 32 only if
    a configuration in the sweep needs it. Pick the smallest w that admits the
    code's coefficients for that (n, r, m, s).
  - mult_XORs(d0, d1, a): multiply byte-region d0 by w-bit constant a in
    GF(2^w) and XOR the product into region d1 (paper Sec 2.3). ALL region
    arithmetic must funnel through this single primitive, because validation
    step 2B counts calls to it.

Target problem metrics (required by the milestone brief):
  - Encode/decode throughput in MB/s (stripe size 32 MB, matching the paper).
  - C = number of mult_XORs() per stripe — the hardware-independent cost metric
    the paper itself uses. This is the metric M2/M3 must reduce.

Codes to cover: RS(n, m) (symmetric, for the M3 gPPM comparison) and
SD^{m,s}_{n,r} (asymmetric, the main target). GRS falls out of SD.

## 2. Correctness validation

### 2A. The invariant H * B = 0

Paper Sec 2.2 Step 4 states that with no faulty block in the stripe,
H * B = 0, and this is the entire justification for B_F = F^-1 * S * B_S.
So it is both the encoder's correctness check and the decoder's precondition.

Test procedure:
  1. Fill the k data sectors with random bytes; encode to produce all parity
     sectors, giving the full block vector B.
  2. Assert H * B == 0 (per-byte, over GF(2^w)) for every configuration. A
     nonzero syndrome means H or the encoder is wrong.
  3. Erase m disks + s additional sectors, decode, and assert the recovered
     B_F is byte-identical to the original.
  4. Re-check H * B == 0 on the reconstructed stripe.

Note: F must be invertible for the chosen failure pattern. Failure patterns are
drawn at random (as the paper does); if F is singular, record it and redraw,
and log the singular-pattern rate — it is a real result worth reporting, since
SD codes are only guaranteed to correct their design failure class.

### 2B. Operation-count validation against the paper's C1

Paper Sec 3.2 gives the closed form for the traditional normal-sequence decode:

  C1 = n*r*(m+s) + m*(m*r + s)*(z-1) + m^2*(r - z)

where z = number of stripe rows containing the s additional faulty sectors.
This is the exact predicted mult_XORs() count. (Paper footnote 3: they derived
it by printing nonzero counts from their own implementation — so an exact match
validates our H construction against theirs, not just our arithmetic.)

Test procedure:
  1. Instrument the decoder with a global counter incremented once per
     mult_XORs() call.
  2. Count Step 4 only, per stripe. Matrix-matrix work (the Step 3 inversion)
     is excluded — paper footnote 2 explicitly ignores it as negligible next to
     matrix-by-block work.
  3. Sweep the paper's full parameter range:
       4 <= n <= 24, 4 <= r <= 24, 1 <= m <= 3, 1 <= s <= 3, 1 <= z <= s
     = 21 * 21 * 3 * 6 = 7938 configurations.
  4. Require an EXACT integer match of measured count vs. C1 for every single
     configuration. Emit a pass/fail table; any mismatch is a bug in H, in the
     failure-pattern placement, or in the counting discipline below.

Two counting rules that must hold or the numbers will not match:
  - C1 = u(F^-1) + u(S) counts NONZERO coefficients. So the decoder must skip
    zero coefficients entirely (no mult_XORs call for a == 0).
  - It counts nonzeros, not non-ones. So a coefficient of 1 STILL counts as one
    mult_XORs. Do NOT special-case a == 1 into a plain XOR in the baseline, or
    the measured count will come in under C1. (The distinction between nonzero
    u(M) and non-one v(M) is exactly what gPPM exploits in M3 via C = u + c*v —
    keep the baseline honest so that gain is visible later.)

For the count sweep, sectors can be small (or the multiply can be a no-op
counter) since only the call count matters — this keeps 7938 configurations
cheap. Throughput measurement is a separate run at 32 MB stripes.

## 3. Milestone 1 deliverables (zip)

  - PDF: problem definition; literature review with proper references (gPPM
    paper plus RS [46], Cauchy RS [11], EVENODD [9], RDP [13], SD/PMDS [10, 41],
    LRC [21, 47], Plank's GF-SIMD work [42], ISA-L [3], APCM [15], EC-Wide [20]);
    baseline throughput/cost results; the 2A and 2B correctness validation
    results.
  - Sequential C implementation + build files + the validation harness.


---

# MILESTONE 1 — FINDINGS SO FAR (appended after implementation)

## Status
Sequential Sec-2.2 baseline is built and passing: `src/{gf,matrix,sd_code,codec,main}.c`,
`make run`. See README.md. Figure-2 example SD^{1,1}_{4,4}(8|1,2) reproduces the
paper exactly: u(S)=22, u(F^-1)=13, C=35=C1. H*B=0 and round-trip both pass.
NOT yet built: the 7938-config 2B sweep, MB/s benchmarking, RS(n,m) construction.

## H structure (confirmed against the paper, not guessed)
R_H = m*r+s, C_H = n*r, column i*n+j = sector b_{i*n+j} (stripe row i, disk j).
  rows m*i+l (0<=i<r, 0<=l<m) : disk parity, H(row, i*n+j) = a_l^j, zero outside
                                stripe row i. Sparse, row-local, n nonzeros.
  rows m*r+l (0<=l<s)         : sector parity, H(row, c) = a_{m+l}^c. Dense, n*r.
Row ordering fixed by Algorithm 1 (addresses rows m*i .. m*i+m-1 as one block).
The sparse/dense asymmetry IS the "asymmetric parity" property PPM exploits in M2.
u(H) = n*r*(m+s), matching C1's first term.

## Coefficient tables -- FOUND, vendored
data/FAST-Coefficients.txt (+ data/License-sd_codes.txt, New BSD).
Source: https://bitbucket.org/jimplank/sd_codes  = paper ref [40], Plank,
"Open Source Encoder and Decoder for SD Erasure Codes", UT-CS-13-704 (2013).
NOTE: web.eecs.utk.edu has a broken TLS chain; fetch via the Bitbucket API
(api.bitbucket.org/2.0/repositories/jimplank/sd_codes/src/HEAD/<file>).
Papers are reachable at library.eecs.utk.edu/files/ut-cs-12-701.pdf.

Format, one line per config:  n m s r w a_0 ... a_{m+s-1}
   4 1 1  4     8   1 2           = SD^{1,1}_{4,4}(8|1,2)      paper Fig. 2
   6 2 2  4     8   1 42 26 61    = SD^{2,2}_{6,4}(8|1,42,26,61) paper Sec. 2.1
Both gPPM worked examples appear verbatim -> this is the table gPPM used.

Coverage: 3969 lines = 21(n) * 3(m) * 3(s) * 21(r), complete grid, no gaps,
all with the correct m+s coefficient count. Crossed with 1 <= z <= s this gives
exactly the 7938 sweep points already in the 2B plan above.

## GAP: w=32 is required for 27% of the sweep
w distribution in the table: w=8 -> 1909 configs, w=16 -> 970, w=32 -> 1090.
src/gf.c implements w=8 and w=16 only, so 1090/3969 configs cannot run yet.
This corrects an earlier assumption: the binding constraint is NOT column count
(n*r <= 576) but CODE EXISTENCE -- Plank's Monte Carlo search found no valid SD
code in a smaller field for those parameters, so w=32 is mandatory there.
GF(2^32) cannot use log tables (4G entries); needs split-table or carry-less
multiply. This is also why the paper's figures show "jagged lines ... switching
between GF(2^8), GF(2^16) and GF(2^32)".
=> Build the sweep in two stages: 2879 configs at w=8/16 now, w=32 backend after.

## Plank's two SD constructions (UT-CS-12-701 Sec. 5)
  "main construction" / Blaum's Code C(1): a_i = 2^i in GF(2^w).
  "random construction": a_0 = 1, remaining a_i arbitrary, found by Monte Carlo.
There is NO general SD construction for arbitrary (n,m,s,r); validity is verified
by enumeration/PMDS theorems. Plank's search range is 4<=n<=24, 1<=m<=3, 1<=s<=3,
r<=24 -- identical to the C1 validity range in Sec. 3.2. Not a coincidence.

## C1 is a GENERIC count -- important for 2B
Probed 15 configs: 13 exact matches (incl. the paper's own SD^{2,2}_{6,4} at
z=1 and z=2). The 2 misses were short by exactly 1 in u(F^-1), on invented
coefficients. Sweeping 200 random coefficient sets per config: mismatches are
ALWAYS UNDER, NEVER OVER.
Cause: algebraically related coefficients make an entry of F^-1 cancel to zero,
so measured C <= C1 always, with equality iff no accidental cancellation.
Consequences for the 2B harness:
  - Drive it from data/FAST-Coefficients.txt, never from invented coefficients.
  - Mismatch DIRECTION is a free diagnostic:
        measured > C1  -> real bug in H or in the counting discipline
        measured < C1  -> coefficient degeneracy (or a wrong-but-valid H)
  - ~2-6% of random coefficient sets leave F singular for a given pattern; the
    harness needs a redraw/skip path and should log the rate. (Table coefficients
    should not be singular for in-spec patterns -- if they are, that is a bug.)
  - Still to check: whether the table's own coefficients ever under-count. The
    main construction a_i = 2^i is exactly the kind of related set that can
    cancel, so 2B's "exact match everywhere" bar must be re-confirmed against
    the real table before it is treated as a pass/fail gate.

## Useful references pulled from the paper
 [40] Plank 2013, open source SD encoder/decoder, UT-CS-13-704  <- the codebase
      gPPM modified; source of data/FAST-Coefficients.txt
 [41] Plank, Blaum, Hafner 2013, "SD codes: Erasure codes designed for how
      storage systems really fail", FAST'13 (ext. version UT-CS-12-701)
 [42] Plank, Greenan, Miller 2013, "Screaming fast Galois field arithmetic using
      Intel SIMD instructions", FAST'13  <- the M3 vectorisation reference
 [31] Li et al. 2015, PPM, ICPP'15  <- the conference precursor to this paper
 [15] APCM, [20] EC-Wide, [29] STAIR codes  <- related-work comparisons


---

# MILESTONE 1 — 2B SWEEP RESULTS (harness built, full range run)

Harness: `src/sweep.c`, `make sweep` -> `sd_sweep`, per-point CSV in
`sweep_results.csv` (columns: n,m,s,r,w,z,u_S,pred_u_S,u_Finv,pred_u_Finv,
measured,C1,delta,count_status,roundtrip). Driven by data/FAST-Coefficients.txt.

## Numbers
  sweep points (config x z) : 7938   <- matches the planned space exactly
    skipped, w=32           : 3057   (no GF(2^32) backend yet)
    skipped, geometry       :  105   (s > z*(n-m): the pattern cannot exist)
    F singular              :    0
    evaluated               : 4776
  2B exact match            : 4592  (96.15%)
     under (measured < C1)  :  184
     over  (measured > C1)  :    0
  2A round-trip pass        : 4650   fail 0   n/a 126 (s > n-m, no parity layout)

## WHY 96.15% AND NOT 100%
C1 splits into two halves, C1 = u(S) + u(F^-1). Both closed forms were derived
algebraically from the Sec. 3.2 formula and are now checked separately by the
harness:

  u(S)    = (m+s)*(n*r - m*r - s)            -> 4776 ok,   0 off   EXACT ALWAYS
  u(F^-1) = (m*r+s)*(m*z+s) + m^2*(r-z)      -> 4592 ok, 184 off   <- all drift

  * u(S) is purely STRUCTURAL: it counts nonzeros of H on the surviving
    columns, which follows from the sparse/dense row structure alone. It cannot
    depend on coefficient values, and it never deviated.
  * u(F^-1) is a GENERIC-POSITION estimate: it assumes no entry of the inverse
    cancels to zero. With Plank's real published coefficients some entries do
    cancel, so the measured count comes in LOWER. Never higher: 0 over-counts
    in 4776 points.

=> C1 is a TIGHT UPPER BOUND in general position, not an identity. This is
   consistent with the paper's own footnote 3 (they derived C1 by printing
   nonzero counts from their implementation, i.e. from whichever coefficients
   they ran). "Exact match for every configuration" is therefore not achievable
   as literally stated, and is the wrong pass/fail bar.

## The deviation is structured, not noise
  m s | match%        m s | match%
  1 1 | 100.00        2 2 |  92.66
  1 2 | 100.00        2 3 |  97.27
  1 3 |  99.42        3 2 |  81.02
  2 1 | 100.00        3 3 |  94.58
  3 1 | 100.00
All 1323 s=1 points match exactly for every m. Cancellation only appears once
m>=2 AND s>=2, i.e. once F^-1 has enough structure to admit it. Worst observed
delta -13 (SD^{3,2}_{5,4}, z=2: measured 147 vs C1 160).

## Correct pass/fail gate for the report
Use, and the harness prints it:
    no OVER counts, no round-trip failures, no singular F
  measured > C1 -> real bug in H or in the counting discipline.
  measured < C1 -> coefficient cancellation, expected, benign.
Current status: all three clean. (sd_sweep still exits 1 whenever any UNDER
exists; flip to over||rt_fail||singular if it is wanted as a CI gate.)

## Independent confirmation of H
0 singular F across 4776 m-disk + s-sector patterns. The SD condition is
precisely "decodes all combinations of m disks and s sectors", so a correct H
plus valid table coefficients must never be singular here. It never was.
Combined with 4650/4650 round-trips, the Sec. 2.2 implementation is validated.

## z-placement rule (decision, was open)
m leftmost disks fail entirely; the s extra faulty sectors spread over stripe
rows 0..z-1 as evenly as possible, packed left to right from the first
surviving disk. Deterministic and reproducible. Feasible iff s <= z*(n-m) and
z <= r; the 105 skips are genuine geometric impossibilities (e.g. n=4, m=3
leaves 1 surviving disk per row, so z=1 cannot host s=3), not failures.
C1 depends only on (n,r,m,s,z), so placement within the z rows should not
change the count -- untested, worth a spot-check if a reviewer asks.

## RESOLVES the earlier open question
The previous section asked whether the table's own coefficients ever
under-count. They do: 184/4776. The main construction a_i = 2^i is exactly the
related-coefficient case that cancels, as suspected.

## Still open
  - GF(2^32) backend -> unlocks the remaining 3057 points (1090 configs, 27%).
    Needs split-table or carry-less multiply; log tables are impossible at 2^32.
  - Throughput benchmarking at 32 MB stripes (MB/s).
  - RS(n,m) construction for the symmetric-code comparison gPPM needs.

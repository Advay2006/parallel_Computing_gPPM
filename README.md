# gPPM — Milestones 1-2: Sequential Baseline and OpenMP PPM

Reference implementation of the traditional sequential erasure-code baseline
and the Partitioned and Parallel Matrix (PPM) decoder described in Sections
2.2 and 3 of:

> Shiyi Li, Qiang Cao, Shenggang Wan, Wen Xia, Changsheng Xie.
> *gPPM: A Generalized Matrix Operation and Parallel Algorithm to Accelerate
> the Encoding/Decoding Process of Erasure Codes.*
> ACM TACO 20(4), Article 51, December 2023.

`ec_recover()` remains the **single-threaded, no-SIMD, normal-sequence-only**
baseline. Milestone 2 adds an SD-specific PPM path that partitions independent
failure groups, recovers them with OpenMP and a fixed matrix-first sequence,
then recovers the dependent remainder with the normal sequence. Dynamic gPPM
selection and SIMD remain later work.

Project plan and milestone breakdown: [`Context.md`](Context.md).
Standalone Milestone 1 report: [`Milestone_1_Report.pdf`](docs/Milestone_1_Report.pdf).
Original literature review with the validated baseline results appended:
[`finalfi_with_baseline_results.pdf`](docs/finalfi_with_baseline_results.pdf).

---

## Quick start

```sh
make           # builds executables under build/
make test      # focused GF/SD unit and integration tests
make run       # paper Figure 2 smoke test
make sweep     # full SD parameter range; writes results/sweep_results.csv
make benchmark # 32 MiB baseline suite; writes results/benchmark_results.csv
make ppm-test      # focused partitioning/OpenMP/Figure 3 tests
make ppm-sweep     # full PPM grid; writes results/ppm_sweep_results.csv
make ppm-benchmark # baseline vs PPM T=1,2,4,8 scaling
make plots         # presentation-ready PNG and SVG charts in results/plots/
make clean
```

## Repository layout

| Path | Contents |
|---|---|
| `src/` | Baseline and PPM implementations and command-line programs |
| `tests/` | Unit and integration tests |
| `data/` | Published FAST coefficient input |
| `docs/` | PDF and DOCX project documents |
| `results/` | Correctness-sweep and benchmark CSV output |
| `build/` | Generated executables |

Expected output (the paper's Figure 2 example, `SD^{1,1}_{4,4}(8 | 1, 2)`):

```
u(S) = 22, u(F^-1) = 13, mult_XORs = 35 (C1 = 35)
2A  H*B == 0 after encode : PASS
2A  recovered == original : PASS
2A  H*B == 0 after decode : PASS
2B  exact match           : PASS
ALL CHECKS PASSED
```

Exit status is 0 only if every check passes, so `make run` is usable as a
regression gate.

---

## What order things run in

The `build/sd_baseline` executable follows the paper's four steps in this call order:

```
main.c
  │
  ├─ 1. gf_init(&gf, w)                        gf.c      build GF(2^w) tables
  │
  ├─ 2. sd_build_H(&code, &gf, &H)             sd_code.c  ── STEP 1 ──
  │        H is (m*r+s) x (n*r)
  │
  ├─ 3. sd_parity_sectors(&code, parity)       sd_code.c  which sectors are parity
  │
  ├─ 4. fill data sectors with random bytes    main.c
  │
  ├─ 5. ec_encode(H, parity, ...)              codec.c   encode == decode with
  │        └─ ec_recover()                                faulty set = parity set
  │             ├─ ec_split()                            ── STEP 2 ──  H -> F, S
  │             ├─ mat_invert()                matrix.c  ── STEP 3 ──  F -> F^-1
  │             └─ ec_decode_normal()                    ── STEP 4 ──
  │                  ├─ T  = S * BS               (u(S) calls to mult_XORs)
  │                  └─ BF = F^-1 * T             (u(F^-1) calls)
  │                       └─ mult_XORs()       gf.c      the counted primitive
  │
  ├─ 6. ec_syndrome_is_zero(H, stripe, ...)    codec.c   VALIDATION 2A
  │
  ├─ 7. wipe the faulty sectors                main.c    simulate the failure
  │
  ├─ 8. ec_recover(H, faulty, ...)             codec.c   STEPS 2-4 again, decoding
  │
  ├─ 9. memcmp against the saved copy          main.c    VALIDATION 2A
  │
  └─ 10. compare mult_XORs against C1_formula  main.c    VALIDATION 2B
```

**Encoding and decoding are the same code path.** Paper footnote 1: encoding is
a special case of decoding. `ec_encode` is a macro aliasing `ec_recover` with
the faulty set set to the parity sectors. Because `H*B = 0` holds by
definition, solving for the parity sectors *is* encoding.

**Build order** (handled by the Makefile, listed here for dependency clarity):
`gf.c` → `matrix.c` → `sd_code.c` → `codec.c` → `main.c`. Each layer depends
only on the ones above it.

---

## File reference

### `src/gf.h` / `src/gf.c` — Galois Field arithmetic

The bottom layer. Everything else is built on it.

* `gf_t` holds discrete-log and doubled antilog tables for GF(2^8) and GF(2^16).
  GF(2^32) cannot use a 2^32-entry log table, so it uses a scalar 8x8 split
  table instead.
* `gf_init` uses primitive polynomial `0x11D` for w=8, `0x1100B` for w=16,
  and GF-Complete's default `0x00400007` reduction polynomial for w=32.
* `gf_mul` / `gf_div` / `gf_inv` / `gf_pow` provide field arithmetic. Powers
  use log tables for w=8/16 and square-and-multiply for w=32.
* **`mult_XORs(d0, d1, a, nbytes, gf)`** — the paper's core primitive
  (Sec. 2.3): multiply region `d0` by the constant `a` over GF(2^w) and XOR the
  product into region `d1`. It remains scalar; w=32 uses a per-constant byte
  table for large regions so the reference backend is practical at 32 MiB.
* `gf_mult_xors_count` — a C11 thread-local counter incremented once per
  `mult_XORs` call. PPM aggregates worker-local values after its barrier.

w=8, w=16, and w=32 cover all 3,969 published FAST SD configurations.

### `src/matrix.h` / `src/matrix.c` — dense matrices over GF(2^w)

* `gf_mat` — row-major `uint32_t` array plus dimensions. Access via the
  `MAT(M, i, j)` macro.
* `mat_nonzeros(M)` — **`u(M)`**, the paper's cost metric: the number of
  nonzero coefficients, which is exactly the number of `mult_XORs` calls a
  matrix-by-block-vector product will issue.
* `mat_non_ones(M)` — **`v(M)`**, the count of elements greater than 1. Unused
  by the Milestone 1 normal sequence; it exists because gPPM's refined cost
  model `C = u + c*v` needs it in Milestone 3.
* `mat_invert(A, gf, out)` — Gauss-Jordan on the augmented `[A | I]`. Returns
  `-1` if `A` is singular, which happens when the failure pattern falls outside
  the code's correctable set.
* `mat_mul(A, B, gf, out)` — small coefficient-matrix multiplication used by
  PPM's fixed matrix-first sequence. It does not count toward region-operation
  metric `C`.

### `src/sd_code.h` / `src/sd_code.c` — **Step 1**, the parity-check matrix

`sd_code_t` carries the parameterisation `SD^{m,s}_{n,r}(w | a_0, …, a_{m+s-1})`.

`sd_build_H` constructs `H`, which is `R_H × C_H` with `R_H = m*r + s` and
`C_H = n*r`. Column `i*n + j` corresponds to sector `b_{i*n+j}` at stripe row
`i`, disk `j`. Two row families:

| Rows | Meaning | Coefficient | Nonzeros/row |
|---|---|---|---|
| `m*i + l`, `0≤i<r`, `0≤l<m` | **disk parity** for stripe row `i` | `H(row, i*n+j) = a_l^(i*n+j)`, zero outside row `i` | `n` (sparse, row-local) |
| `m*r + l`, `0≤l<s` | **sector parity**, spans the whole stripe | `H(row, c) = a_{m+l}^c` | `n*r` (dense) |

That sparse/dense asymmetry is what makes SD an *asymmetric parity* code, and
it is the structural fact PPM exploits in Milestone 2: the row-local rows are
what produce independent faulty blocks. The row ordering (`m` consecutive rows
per stripe row) is fixed by the paper's Algorithm 1, which addresses rows
`m*i … m*i+m-1` as the block belonging to stripe row `i`.

`sd_parity_sectors` returns the `m*r + s` coding-sector indices: the `m`
rightmost disks in full, plus the `s` highest-numbered sectors outside those
disks. Extra parity may span multiple rows when `s > n-m`.

### `src/codec.h` / `src/codec.c` — **Steps 2–4**, the codec

* **`ec_split`** (Step 2) — partitions `H` by column into `F` (faulty columns)
  and `S` (surviving columns), and reports the surviving column indices.
  Rejects duplicate or out-of-range indices.
* Step 3 is `mat_invert` from the matrix layer.
* **`ec_decode_normal`** (Step 4) — computes `BF = F^-1 * (S * BS)` in the
  **normal sequence**: first `T = S*BS` into a scratch array of `R_H` sectors,
  then `BF = F^-1 * T`. All of `T` is finalised before any faulty sector is
  written, so overwriting them in place is safe. Zero coefficients are skipped;
  nonzero ones each cost one `mult_XORs`.
* **`ec_decode_matrix_first`** — computes `G = F^-1*S` and then `BF = G*BS`.
  PPM uses this fixed sequence for independent submatrices; it does not change
  the baseline `ec_recover()` path.
* **`ec_recover`** — Steps 2–4 together. Resets the counter immediately before
  Step 4, so `decode_stats_t.mult_xors` measures Step 4 only. The Step 3
  inversion is excluded on purpose: paper footnote 2 treats matrix-on-matrix
  work as negligible against matrix-on-block work. Returns `-1` if `F` is
  singular.
* **`ec_encode`** — macro alias for `ec_recover`.
* **`ec_syndrome_is_zero`** — validation 2A. Computes `H*B` across the whole
  stripe and checks every syndrome sector is all-zero.

`decode_stats_t` reports `u(S)`, `u(F^-1)`, and the measured call count. The
identity `mult_xors == u_S + u_Finv` should always hold.

### `src/ppm.h` / `src/ppm.c` — Milestone 2 PPM

For each physical stripe row, PPM counts failed sectors. A row containing
exactly `m` failures forms an independent group because its `m` row-local
equations solve those failures without data from another failed row. These
groups use fixed matrix-first decoding in an OpenMP loop. The loop's implicit
barrier completes before `Hrest` is decoded normally, because its dense
equations use the independently recovered sectors.

The paper's Figure 3 example partitions `{2,6,10,13,14}` into three independent
groups and a two-sector remainder. The implementation reproduces baseline
`C=35` and PPM `C=29`.

### `src/main.c` — the Figure 2 example

Runs `SD^{1,1}_{4,4}(8 | 1, 2)` with 4 KiB sectors: builds and prints `H`,
encodes random data, checks `H*B = 0`, saves a copy, wipes the faulty set
`{2, 6, 10, 13, 14}` (disk 2 lost plus the extra sector `b13` — the paper's
exact scenario), decodes, and compares. `C1_formula` implements the paper's
Sec. 3.2 closed form:

```
C1 = n*r*(m+s) + m*(m*r+s)*(z-1) + m^2*(r-z)
```

For this configuration it gives 35, and the measured count is 35, decomposing
as `u(S) = 22` + `u(F^-1) = 13`. Both halves were also verified by hand against
the paper before the code was written.

### `src/coefficients.c`, `src/sweep.c`, and `src/benchmark.c`

* `coefficients.c` strictly loads the complete published FAST coefficient grid
  and rejects malformed, duplicate, missing, or out-of-range records.
* `sweep.c` validates every feasible `(configuration, z)` point and writes the
  detailed operation counts and round-trip status to `results/sweep_results.csv`.
* `benchmark.c` runs the reproducible 32 MiB sequential encode/decode suite and
  writes every timed trial to `results/benchmark_results.csv`.
* `tests/test_gf.c` and `tests/test_sd.c` cover field arithmetic, the upstream
  FAST matrix, multi-row parity placement, and a GF(2^32) round trip.

---

## Two counting rules

Validation 2B compares the measured `mult_XORs` count against `C1`. Two rules
must hold or the numbers will not line up, and both are easy to violate with a
well-meaning optimisation:

1. **Never call `mult_XORs` with `a == 0`.** `C1 = u(F^-1) + u(S)` counts
   *nonzero* coefficients. Callers skip zeros; `mult_XORs` asserts on zero.
2. **Always call it with `a == 1`.** `C1` counts nonzeros, *not* non-ones.
   The primitive may perform a direct XOR internally, but the caller must still
   invoke it so the operation is counted and the `u` versus `v` distinction
   remains available to later milestones.

---

## Coefficient quality matters

`C1` is the **generic** nonzero count. Measured `C` can come in *below* it when
algebraically related coefficients cause an entry of `F^-1` to cancel to zero.

The completed sweep evaluates one deterministic failure layout for every
geometrically feasible `(configuration, z)` point: 7,833 layouts from 3,969
published configurations. It finds 7,649 exact C1 matches and 184 under-counts.
Every under-count comes from a zero created in `F^-1`; `u(S)` is exact in all
7,833 tested layouts. There are no over-counts, singular matrices, internal
count mismatches, encode failures, or round-trip failures.

Consequences for the Milestone 1 sweep:

* Use **valid published SD coefficient sets** (Plank's SD tables). Arbitrary
  values will produce spurious 2B failures.
* **Mismatch direction is a diagnostic.** Measured `> C1` means a real bug in
  `H` or in the counting discipline. Measured `< C1` most likely means
  degenerate coefficients.
* Published coefficients produce no singular matrices for the tested patterns.

---

## Deliberate omissions

The baseline remains intentionally unchanged. These generalized optimizations
remain later work:

| Omitted | Belongs to |
|---|---|
| Dynamic normal-vs-matrix-first selection | Milestone 3 (gPPM) |
| General log-table partitioning beyond SD | Later generalized work |
| SIMD / SSE / AVX in `mult_XORs` | Milestone 3 |
| `C = u + c*v` cost model and dynamic sequence selection | Milestone 3 |

---

## Completed validation

`make sweep` covers 7,938 `(configuration, z)` points: 105 are geometrically
impossible, and one deterministic layout for each of the 7,833 feasible points
passes. This is full parameter-grid coverage, not exhaustive enumeration of
every possible disk/sector placement. Full details are in
`results/sweep_results.csv`.

`make ppm-sweep` evaluates the same 7,833 feasible points with PPM at `T=1`
and `T=4`. All recoveries, syndrome checks, partition checks, and thread-count
operation-count comparisons pass. Full details are in
`results/ppm_sweep_results.csv`.

`make benchmark` runs ten encode and ten decode trials for each `n=16`, `r=16`,
`z=1`, `m,s in {1,2,3}` configuration using an exact 32 MiB codeword. All 180
trials pass validation. Mean useful-data throughput on the recorded Intel Core
Ultra 9 185H / GCC 11.4.0 `-O2` run was:

| m | s | Field | C | Encode MiB/s | Decode MiB/s |
|---:|---:|---|---:|---:|---:|
| 1 | 1 | GF(2^8) | 527 | 852.60 | 854.58 |
| 1 | 2 | GF(2^16) | 783 | 634.68 | 631.51 |
| 1 | 3 | GF(2^32) | 1039 | 673.01 | 671.77 |
| 2 | 1 | GF(2^8) | 828 | 444.98 | 447.81 |
| 2 | 2 | GF(2^16) | 1084 | 399.48 | 401.21 |
| 2 | 3 | GF(2^32) | 1340 | 479.27 | 476.19 |
| 3 | 1 | GF(2^8) | 1159 | 282.56 | 275.55 |
| 3 | 2 | GF(2^16) | 1415 | 276.56 | 279.68 |
| 3 | 3 | GF(2^32) | 1671 | 352.91 | 349.51 |

The paper used different CPUs and SIMD acceleration, so these values are the
local scalar baseline for later speedup calculations, not a direct reproduction
of the paper's absolute rates. RS construction remains future gPPM comparison
work rather than part of this SD baseline.

`make ppm-benchmark` compares baseline decoding with PPM at `T=1,2,4,8` using
the same 32 MiB codeword and failure layout. All 450 measured trials pass.
Median speedups from the current run are:

| m | s | Field | T=1 | T=2 | T=4 | T=8 |
|---:|---:|---|---:|---:|---:|---:|
| 1 | 1 | GF(2^8) | 1.13x | 1.31x | 1.34x | 1.41x |
| 1 | 2 | GF(2^16) | 1.03x | 1.10x | 1.18x | 1.22x |
| 1 | 3 | GF(2^32) | 1.04x | 1.14x | 1.20x | 1.18x |
| 2 | 1 | GF(2^8) | 1.02x | 1.25x | 1.65x | 1.77x |
| 2 | 2 | GF(2^16) | 1.08x | 1.35x | 1.48x | 1.46x |
| 2 | 3 | GF(2^32) | 1.17x | 1.36x | 1.39x | 1.49x |
| 3 | 1 | GF(2^8) | 1.19x | 1.66x | 2.10x | 2.33x |
| 3 | 2 | GF(2^16) | 1.19x | 1.53x | 1.80x | 1.67x |
| 3 | 3 | GF(2^32) | 1.24x | 1.53x | 1.64x | 1.73x |

`T=1` isolates algorithmic work reduction; higher thread counts add OpenMP
parallelism. Full trial data, effective thread counts, throughput, speedup, and
efficiency are in `results/ppm_benchmark_results.csv`.

## Presentation plots

Run `make plots` after the baseline sweep, PPM sweep, and PPM benchmark. The
Python visualizer uses Matplotlib and writes both high-resolution PNG and
editable SVG versions to `results/plots/`:

| Plot | Purpose |
|---|---|
| `ppm_speedup_scaling` | Speedup versus OpenMP thread count for all nine benchmark configurations |
| `ppm_throughput_scaling` | Baseline and PPM useful-data throughput |
| `ppm_work_reduction` | Heatmap of algorithmic operation-count reduction at `T=1` |
| `ppm_sweep_work_reduction` | Work-reduction distribution across all 7,833 feasible layouts |
| `ppm_figure3_cost` | Presentation graphic for the paper's `35 -> 29` example |

The SVG files are suitable for editing or direct insertion into presentation
software; the PNG files are rendered at 240 DPI.

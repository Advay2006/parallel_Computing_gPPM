# gPPM — Milestone 1: Sequential Baseline

Reference implementation of the **traditional (baseline) erasure-code
encode/decode** described in Section 2.2 of:

> Shiyi Li, Qiang Cao, Shenggang Wan, Wen Xia, Changsheng Xie.
> *gPPM: A Generalized Matrix Operation and Parallel Algorithm to Accelerate
> the Encoding/Decoding Process of Erasure Codes.*
> ACM TACO 20(4), Article 51, December 2023.

This is the **single-threaded, no-SIMD, normal-sequence-only** model that
Milestones 2 (PPM / OpenMP) and 3 (gPPM / vectorised) are measured against.
Anything that would make it faster is deliberately left out — see
[Deliberate omissions](#deliberate-omissions).

Project plan and milestone breakdown: [`Context.md`](Context.md).
Milestone report with the preserved literature review and appended results:
[`final_report.pdf`](final_report.pdf).

---

## Quick start

```sh
make           # builds sd_baseline, sd_sweep, and sd_benchmark
make test      # focused GF/SD unit and integration tests
make run       # paper Figure 2 smoke test
make sweep     # full SD parameter range; writes sweep_results.csv
make benchmark # 32 MiB baseline suite; writes benchmark_results.csv
make clean
```

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

The `sd_baseline` executable follows the paper's four steps in this call order:

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
* `gf_mult_xors_count` — a global counter incremented once per `mult_XORs`
  call. This is the instrumentation that validation 2B reads.

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
  detailed operation counts and round-trip status to `sweep_results.csv`.
* `benchmark.c` runs the reproducible 32 MiB sequential encode/decode suite and
  writes every timed trial to `benchmark_results.csv`.
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

Not bugs — these are later milestones, and adding them now would blur the
baseline:

| Omitted | Belongs to |
|---|---|
| `matrix_first` sequence (`F^-1*S` first) | Milestone 3 (gPPM) |
| Log table, matrix partitioning, independent sub-matrices | Milestone 2 (PPM) |
| Threading | Milestone 2 |
| SIMD / SSE / AVX in `mult_XORs` | Milestone 3 |
| `C = u + c*v` cost model and dynamic sequence selection | Milestone 3 |

`gf_mult_xors_count` is a single global. Milestone 2 must make it per-thread
before PPM's threads touch it.

---

## Completed validation

`make sweep` covers 7,938 `(configuration, z)` points: 105 are geometrically
impossible, and one deterministic layout for each of the 7,833 feasible points
passes. This is full parameter-grid coverage, not exhaustive enumeration of
every possible disk/sector placement. Full details are in `sweep_results.csv`.

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

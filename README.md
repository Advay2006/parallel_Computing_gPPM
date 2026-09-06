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

---

## Quick start

```sh
make          # builds ./sd_baseline
make run      # builds and runs
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

There is one executable. The ordering that matters is the **call order inside
it**, which follows the paper's four steps exactly.

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

* `gf_t` holds a discrete-log table (`logt`) and an antilog table (`expt`) for
  GF(2^w), w ∈ {8, 16}. `expt` is stored **doubled** (`2*nz+1` entries) so
  `gf_mul` can index `logt[a]+logt[b]` directly without a modulo.
* `gf_init` walks the powers of the generator `x = 2` under the primitive
  polynomial (`0x11D` for w=8, `0x1100B` for w=16), filling both tables.
* `gf_mul` / `gf_div` / `gf_inv` / `gf_pow` — field arithmetic via table
  lookup. `gf_pow(a, e)` computes `expt[(e * logt[a]) mod nz]`.
* **`mult_XORs(d0, d1, a, nbytes, gf)`** — the paper's core primitive
  (Sec. 2.3): multiply region `d0` by the constant `a` over GF(2^w) and XOR the
  product into region `d1`. Scalar loop, one table lookup per element.
* `gf_mult_xors_count` — a global counter incremented once per `mult_XORs`
  call. This is the instrumentation that validation 2B reads.

w=8 and w=16 cover the whole Milestone 1 sweep (`n*r ≤ 24*24 = 576` columns).

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
| `m*i + l`, `0≤i<r`, `0≤l<m` | **disk parity** for stripe row `i` | `H(row, i*n+j) = a_l^j`, zero outside row `i` | `n` (sparse, row-local) |
| `m*r + l`, `0≤l<s` | **sector parity**, spans the whole stripe | `H(row, c) = a_{m+l}^c` | `n*r` (dense) |

That sparse/dense asymmetry is what makes SD an *asymmetric parity* code, and
it is the structural fact PPM exploits in Milestone 2: the row-local rows are
what produce independent faulty blocks. The row ordering (`m` consecutive rows
per stripe row) is fixed by the paper's Algorithm 1, which addresses rows
`m*i … m*i+m-1` as the block belonging to stripe row `i`.

`sd_parity_sectors` returns the `m*r + s` coding-sector indices: the `m`
rightmost disks in full, plus `s` extra sectors in the last stripe row working
leftwards. Requires `s ≤ n - m`.

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

---

## Two counting rules

Validation 2B compares the measured `mult_XORs` count against `C1`. Two rules
must hold or the numbers will not line up, and both are easy to violate with a
well-meaning optimisation:

1. **Never call `mult_XORs` with `a == 0`.** `C1 = u(F^-1) + u(S)` counts
   *nonzero* coefficients. Callers skip zeros; `mult_XORs` asserts on zero.
2. **Always call it with `a == 1`.** `C1` counts nonzeros, *not* non-ones.
   Special-casing `a == 1` into a plain XOR would push the measured count below
   `C1` — and would also erase the `u` vs `v` distinction that gPPM monetises
   in Milestone 3. Keep the baseline honest so that gain stays visible.

---

## Coefficient quality matters

`C1` is the **generic** nonzero count. Measured `C` can come in *below* it when
algebraically related coefficients cause an entry of `F^-1` to cancel to zero.

Probing 15 configurations, 13 matched `C1` exactly — including the paper's own
`SD^{2,2}_{6,4}(8 | 1, 42, 26, 61)` at `z=1` and `z=2`. The two misses were both
short by exactly 1 in `u(F^-1)`, both on invented coefficient sets. Sweeping
200 random coefficient sets per configuration, mismatches were **always under,
never over**.

Consequences for the Milestone 1 sweep:

* Use **valid published SD coefficient sets** (Plank's SD tables). Arbitrary
  values will produce spurious 2B failures.
* **Mismatch direction is a diagnostic.** Measured `> C1` means a real bug in
  `H` or in the counting discipline. Measured `< C1` most likely means
  degenerate coefficients.
* Roughly 2–6% of random coefficient sets leave `F` singular for a given
  pattern, so the sweep harness needs a redraw path and should log the rate.

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

## Not yet built

* The full 2B sweep harness over `4≤n≤24, 4≤r≤24, 1≤m≤3, 1≤s≤3, 1≤z≤s`
  (7938 configurations), with valid coefficient tables.
* Throughput benchmarking at 32 MB stripes (MB/s).
* RS(n, m) construction for the symmetric-code comparison gPPM needs.

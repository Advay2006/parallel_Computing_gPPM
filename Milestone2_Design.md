# Milestone 2 Design: PPM Partitioning and OpenMP Scaling

## 1. Goal

Milestone 2 converts the existing sequential SD-code decoder into a
Partitioned and Parallel Matrix (PPM) decoder while preserving the Milestone 1
implementation as the baseline.

The completed milestone must provide:

- SD-specific failure partitioning into independent submatrices and a dependent
  remainder.
- Fixed `matrix_first` decoding for independent submatrices, as prescribed by
  the paper's PPM algorithm.
- Normal-sequence decoding for the dependent remainder.
- OpenMP parallel recovery of independent submatrices.
- Thread-safe operation counting.
- Byte-equality and syndrome validation across failure layouts and thread
  counts.
- Reproduction of the paper's Figure 3 result: `C = 35` for the sequential
  baseline and `C = 29` for PPM.
- Baseline-versus-PPM throughput and scaling results.

The existing `ec_recover()` implementation remains the sequential reference.

## 2. Milestone Boundary

The paper's PPM algorithm requires independent submatrices to use the
`matrix_first` sequence. Therefore, Milestone 2 includes the matrix
multiplication and fixed `matrix_first` primitives needed by PPM.

Milestone 2 does not choose calculation sequences dynamically. The sequence is
fixed by the PPM algorithm:

```text
independent submatrix Hi -> matrix_first
dependent Hrest          -> normal
```

The following work remains in Milestone 3:

- Dynamic normal-versus-`matrix_first` selection.
- The refined `C = u + c*v` cost model.
- Whole-matrix versus partitioned cost selection.
- Generalized PPM support for symmetric parity codes such as RS.
- SIMD/SSE/AVX implementations of region arithmetic.
- Hybrid OpenMP and SIMD optimization.

## 3. Existing Baseline to Preserve

The baseline recovery path is:

```text
ec_recover()
    ec_split()
    mat_invert()
    ec_decode_normal()
```

`ec_recover()` must remain sequential and retain its current behavior and
statistics. PPM will be exposed through a separate entry point. This separation
is necessary for credible correctness and performance comparisons.

The existing validation commands remain regression gates:

```sh
make test
make run
make sweep
make benchmark
```

## 4. High-Level PPM Algorithm

For `SD(n, r, m, s)`, the parity-check matrix contains:

- `m` row-local equations for each of the `r` stripe rows.
- `s` dense sector-parity equations after the row-local equations.

For every physical stripe row:

- Exactly `m` failed sectors means that row's `m` failures form an independent
  group.
- More than `m` failed sectors means that row belongs to the dependent
  remainder.

For the supported full-disk-plus-sector failure model, the number of
independent groups is `p = r - z`, where `z` is the number of stripe rows with
additional sector failures.

The recovery sequence is:

```text
1. Validate the code, matrix, failure set, sector size, and thread count.
2. Count failed sectors in each physical stripe row.
3. Build one Hi from the m row-local equations for each row with m failures.
4. Build Hrest from:
       - row-local equations for rows with more than m failures, and
       - all s dense sector-parity equations.
5. Recover H0...Hp-1 with fixed matrix_first decoding in an OpenMP loop.
6. Wait for the OpenMP loop's implicit barrier.
7. Treat the recovered independent sectors as survivors.
8. Recover the remaining failures from Hrest with normal decoding.
9. Aggregate operation counts and return PPM statistics.
```

The independent tasks write disjoint faulty sectors. Because their row-local
equations have zero coefficients outside their own physical stripe row, they do
not read sectors written by other independent tasks.

The barrier before decoding `Hrest` is mandatory. The dense equations in
`Hrest` use the independently recovered sectors as surviving inputs.

## 5. Implementation Stages

### Stage 1: Preserve and Recheck Milestone 1

Before changing behavior:

1. Build the repository from a clean build directory.
2. Run all existing tests.
3. Run the Figure 2 smoke test.
4. Run the correctness sweep.
5. Keep the existing benchmark executable and CSV format available as the
   baseline.

Definition of done:

- Existing tests and validation commands pass before M2 changes.
- The baseline Figure 2 count remains `35` throughout Milestone 2.

### Stage 2: Make Operation Counting Thread-Local

#### `src/gf.h`

Change the instrumentation declaration from a process-global counter to C11
thread-local storage:

```c
extern _Thread_local uint64_t gf_mult_xors_count;
```

Keep `gf_count_reset()` and `gf_count_get()` as the public counter interface.
Update comments to state that each thread owns its counter.

#### `src/gf.c`

Change the definition to:

```c
_Thread_local uint64_t gf_mult_xors_count = 0;
```

Do not use an OpenMP atomic increment in `mult_XORs()`. An atomic operation in
the hottest region-arithmetic path would add shared contention and distort the
scaling benchmark.

#### Counting rule for PPM workers

Each independent job resets and reads the counter on the worker executing that
job:

```text
gf_count_reset()
decode one independent submatrix
job_count = gf_count_get()
```

After the parallel loop, the caller sums the per-job counts. The remainder is
decoded after another reset and its count is added to the independent total.

Definition of done:

- All Milestone 1 operation counts remain unchanged.
- Concurrent workers do not update shared counter state.
- PPM's aggregated count equals the sum of the matrices actually used for
  block-vector multiplication.

### Stage 3: Add Small Matrix Multiplication

#### `src/matrix.h`

Declare a dense GF matrix multiplication function:

```c
int mat_mul(const gf_mat *A, const gf_mat *B,
            const gf_t *gf, gf_mat *out);
```

The function computes `out = A * B`, validates `A->cols == B->rows`, and
allocates `out`.

#### `src/matrix.c`

Implement row-major matrix multiplication over `GF(2^w)` using `gf_mul()` and
XOR accumulation.

Requirements:

- Skip zero operands where practical.
- Detect invalid dimensions and allocation overflow through existing matrix
  allocation checks.
- Free partially allocated output on failure.
- Do not call `mult_XORs()` and do not change the region-operation counter.

Matrix-by-matrix work is excluded from the paper's `C` metric because these
coefficient matrices are small compared with data regions.

#### Matrix tests

Add focused tests for:

- Identity multiplication.
- A known product over GF(2^8).
- Dimension mismatch rejection.
- Products containing zero coefficients.
- Compatibility with the matrices used by the Figure 3 example.

These tests may be added to an existing matrix-related test or included in the
new PPM test if a separate matrix test executable would add unnecessary build
complexity.

Definition of done:

- Matrix multiplication produces known GF results.
- Matrix multiplication does not alter `gf_mult_xors_count`.

### Stage 4: Add Fixed `matrix_first` Decoding

#### `src/codec.h`

Expose a reusable fixed-sequence decoder:

```c
int ec_decode_matrix_first(const gf_mat *Finv, const gf_mat *S,
                           const int *faulty, const int *surviving,
                           uint8_t *stripe, size_t sector_bytes,
                           const gf_t *gf);
```

Document that it computes:

```text
G  = Finv * S
BF = G * BS
```

Its measured block-vector cost is `u(G)`.

#### `src/codec.c`

Implement `ec_decode_matrix_first()` using `mat_mul()` and `mult_XORs()`.

Requirements:

- Validate matrix dimensions before decoding.
- Allocate `G` with `mat_mul()`.
- Zero each faulty destination before accumulating its result.
- Skip zero coefficients.
- Continue calling `mult_XORs()` for coefficient `1`.
- Preserve the mapping from matrix columns to global stripe-sector indices.
- Free `G` on all exits.

Do not modify the behavior or calculation sequence of `ec_recover()`.

#### Fixed-sequence tests

For the same invertible recovery problem, verify that:

- Normal and `matrix_first` decoding recover identical bytes.
- Both results have a zero syndrome.
- The measured `matrix_first` count equals `u(Finv * S)`.
- Matrix multiplication itself is not counted as a region operation.

Definition of done:

- A caller can reuse `ec_split()`, `mat_invert()`, and either decoding sequence.
- Existing baseline results remain unchanged.

### Stage 5: Add the PPM Public Interface

#### New `src/ppm.h`

Define a PPM statistics structure. At minimum it should report:

```c
typedef struct {
    int requested_threads;
    int effective_threads;
    int independent_groups;
    int dependent_failures;
    uint64_t independent_mult_xors;
    uint64_t remainder_mult_xors;
    uint64_t mult_xors;
} ppm_stats_t;
```

Expose an SD-specific recovery function:

```c
int ppm_recover_sd(const sd_code_t *code, const gf_mat *H,
                   const int *faulty, int nf,
                   uint8_t *stripe, size_t sector_bytes,
                   const gf_t *gf, int requested_threads,
                   ppm_stats_t *stats);
```

The API takes `sd_code_t` because SD row grouping cannot be inferred safely
from dimensions alone.

If PPM encoding is benchmarked, provide a macro matching the existing encoding
convention:

```c
#define ppm_encode_sd(code, H, parity, np, stripe, sb, gf, threads, stats) \
        ppm_recover_sd((code), (H), (parity), (np), (stripe), (sb), \
                       (gf), (threads), (stats))
```

#### Error contract

Return `0` on success and `-1` on invalid input, allocation failure, singular
submatrix, or worker failure. As with `ec_recover()`, callers must not assume
the stripe is unchanged after a decoding failure unless preparation is
completed before any worker writes data.

Definition of done:

- PPM has a separate, documented API.
- Baseline statistics and PPM statistics are not conflated.

### Stage 6: Implement SD Partitioning

#### New `src/ppm.c`

Add input validation for:

- Non-null required pointers.
- Positive `requested_threads`.
- `H->rows == m*r+s` and `H->cols == n*r`.
- `nf == m*r+s` for the current worst-case SD recovery model.
- Fault indices in range and without duplicates.
- Sector size greater than zero and aligned to `w/8` bytes.
- Valid positive SD parameters.

Build an `is_faulty` lookup array and count failures per physical stripe row.
The independent criterion is exactly `row_fault_count == m`. The paper's prose,
Figure 3, and `p = r-z` analysis establish this rule.

For each independent physical row:

1. Copy the corresponding `m` row-local rows from `H` into `Hi`.
2. Keep the original `H->cols` columns for Milestone 2.
3. Record the `m` faulty global sector indices for that row.
4. Call `ec_split(Hi, group_faulty, m, ...)`.
5. Invert `Fi`.
6. Store all matrices and index mappings needed by the worker.

Keeping global columns avoids a local-to-global mapping layer. `Si` contains
many zero columns, but zero coefficients are skipped and do not add
`mult_XORs()` operations. This favors a smaller, safer implementation for M2.

Build `Hrest` from:

- The `m` row-local rows for every physical row containing more than `m`
  failures.
- All `s` dense rows at the bottom of `H`.

The dependent fault list contains only faults in the rows with additional
sector failures. Independently recovered faults are not columns of `Frest`;
they become surviving columns in `Srest`.

For valid full-disk-plus-sector layouts:

```text
Hrest rows == dependent fault count == m*z+s
```

This makes `Frest` square and invertible for a correctable pattern.

#### Preparation before writes

Prefer constructing, splitting, and inverting all independent jobs and the
remainder before starting the parallel data-recovery loop. This detects
allocation or singularity failures before any worker changes the stripe.

Definition of done:

- Figure 3 produces three independent groups: `{2}`, `{6}`, and `{10}`.
- Its dependent set is `{13, 14}`.
- Existing deterministic layouts produce `p = r-z`.
- No worker requires a compact-column mapping.

### Stage 7: Make PPM Correct at `T=1`

Before enabling parallel execution, execute the independent jobs serially with
fixed `matrix_first`, followed by normal recovery of `Hrest`.

The Figure 3 test must verify:

```text
configuration: SD(4,4,m=1,s=1,w=8 | 1,2)
faults:        {2,6,10,13,14}
groups:        {2}, {6}, {10}
remainder:     {13,14}
baseline C:    35
PPM C:         29
```

The `29` operations consist of matrix-first block-vector work for the three
independent groups plus normal-sequence work for `Hrest`.

Definition of done:

- `ppm_recover_sd(..., 1, ...)` returns the exact original bytes.
- The recovered stripe has zero syndrome.
- The operation count is `29` for Figure 3.
- Baseline `ec_recover()` still reports `35` for the same failure pattern.

### Stage 8: Add OpenMP Parallel Recovery

#### `src/ppm.c`

Parallelize only the independent-job decode loop:

```c
#pragma omp parallel for num_threads(effective_threads) schedule(static)
for (int i = 0; i < p; i++) {
    /* reset this worker's TLS counter */
    /* decode Hi with fixed matrix_first */
    /* store this job's return code and count */
}

/* implicit barrier */
/* check all worker return codes */
/* decode Hrest with the normal sequence */
```

Set:

```text
effective_threads = min(requested_threads, p)
```

When `p == 0`, skip the parallel loop and decode the full remainder. When
`p == 1`, execute correctly without expecting speedup. Do not start `Hrest`
concurrently with independent jobs.

Worker rules:

- Each worker writes only its group's faulty sectors.
- Job matrices, scratch buffers, return codes, and counts are worker-local or
  stored in separate per-job records.
- Workers do not return from inside the OpenMP region.
- A failed worker records an error; the caller checks errors after the barrier.
- No critical section is used around region arithmetic.
- The shared `gf_t` is read-only during recovery.

Definition of done:

- Results at `T=1,2,4,...` are byte-identical.
- Operation counts do not change with thread count.
- ThreadSanitizer or another available race-checking method reports no data
  races in project-owned state, where compatible with the OpenMP runtime.

### Stage 9: Add PPM Correctness Tests

#### New `tests/test_ppm.c`

Add reusable test helpers that:

1. Build `H`.
2. Fill non-parity sectors deterministically.
3. Encode a golden stripe with the baseline encoder.
4. Save the golden stripe.
5. Wipe the selected failed sectors.
6. Recover with PPM.
7. Compare every byte with the golden stripe.
8. Check `ec_syndrome_is_zero()`.
9. Verify PPM counts and partition statistics.

Required cases:

- The paper's Figure 3 pattern with `35 -> 29` and `p=3`.
- Thread counts `1`, `2`, and `4` for the same layout.
- `w=8`, `w=16`, and `w=32`.
- `m=1`, `m=2`, and `m=3`.
- Multiple `s` and `z` values.
- Cases with no usable parallelism, one independent group, and multiple
  independent groups when valid configurations are available.
- Duplicate and out-of-range fault rejection.
- Incorrect failure count, incompatible dimensions, invalid sector alignment,
  and invalid thread count rejection.

Every successful case must assert:

- Exact byte equality.
- Zero syndrome.
- The same result for every tested thread count.
- The same PPM operation count for every tested thread count.
- `mult_xors == independent_mult_xors + remainder_mult_xors`.

Do not use syndrome validation's later counter value as the recovery count;
capture statistics before syndrome validation changes the calling thread's
counter.

Definition of done:

- All focused PPM tests pass repeatedly.
- Figure 3 is an explicit algorithmic validation, not only a round-trip test.

### Stage 10: Add a PPM Correctness Sweep

The existing `src/sweep.c` validates the baseline across the published
coefficient grid. Preserve that executable and output.

Preferred implementation: add a separate `src/ppm_sweep.c` and
`results/ppm_sweep_results.csv` so baseline results remain reproducible.

For every feasible existing `(configuration, z)` point:

- Encode the golden stripe with the existing baseline path.
- Recover with PPM at `T=1` and at least one multi-threaded setting.
- Compare with the golden stripe.
- Check the syndrome.
- Check partition statistics, including `p = r-z` for the generated layout.
- Verify measured PPM count against the nonzero counts of the actual
  coefficient matrices used.
- Record singular, allocation, count, byte, and syndrome failures separately.

Published coefficients may produce cancellations. Therefore, compare measured
work with actual generated matrices rather than requiring a generic formula to
match exactly. An operation count above its actual matrix-derived count is a
failure.

Definition of done:

- Every feasible baseline sweep point also recovers correctly with PPM.
- No thread-count-dependent result or count difference occurs.

### Stage 11: Add Scaling Benchmarks

#### `src/benchmark.c`

Extend or carefully refactor the existing benchmark so baseline and PPM use:

- The same generated golden stripe.
- The same failure layout.
- The same exact 32 MiB codeword.
- The same useful-data byte count.
- One warm-up before measured trials.
- The same number of measured trials.
- Validation, checksums, and output outside the timed interval.

The timed interval for both algorithms must include equivalent end-to-end
recovery work: partition/split, allocation, inversion, and block recovery. Do
not precompute PPM partitions or inverses outside timing unless equivalent
baseline preprocessing is also excluded.

At minimum, benchmark decoding for:

```text
baseline sequential
PPM T=1
PPM T=2
PPM T=4
PPM T=8
```

Optionally include `T=15` for the current `r=16, z=1` setup because it exposes
`p=15` independent groups. The benchmark must cap the effective thread count at
`p`.

Encoding may also be measured through PPM if its partitioning and validation
are covered, but decoding is the primary M2 scaling result.

#### Benchmark output

Write a separate M2 CSV, such as `results/ppm_benchmark_results.csv`, with at
least:

```text
operation
implementation
n
m
s
r
w
z
requested_threads
effective_threads
independent_groups
trial
seconds
data_MiB_s
codeword_MiB_s
measured_C
baseline_C
speedup
efficiency
checksum
status
```

Use:

```text
speedup(T)    = baseline_seconds / ppm_seconds(T)
efficiency(T) = speedup(T) / effective_threads
improvement   = PPM_throughput / baseline_throughput - 1
```

For noisy trial-level data, calculate speedup from matching aggregate
statistics, preferably medians, and document the method. Do not pair unrelated
individual trial timings as if they were synchronized measurements.

The output must make two effects visible:

- Baseline versus PPM `T=1`: algorithmic work reduction.
- PPM `T=1` versus PPM `T=N`: parallel scaling.

Definition of done:

- Every timed result passes byte, syndrome, count, and checksum validation.
- The CSV contains sufficient data to plot throughput, speedup, and efficiency.
- The original baseline benchmark remains runnable.

### Stage 12: Update the Build

#### `Makefile`

Add configurable OpenMP flags:

```make
OMPFLAGS ?= -fopenmp
```

Add `src/ppm.c` and `src/ppm.h` to PPM-specific dependency lists. Prefer
separate PPM executables so the original baseline binaries remain clearly
identified.

Add targets similar to:

```text
ppm-test       build and run tests/test_ppm.c
ppm-sweep      run the PPM correctness sweep
ppm-benchmark  run baseline-versus-PPM scaling benchmark
```

Update `test` to include `ppm-test` once the implementation is stable. Decide
whether `all` should include the potentially slower benchmark/sweep binaries,
but never execute benchmarks as part of `all`.

Compile every executable that links PPM/OpenMP code with `$(OMPFLAGS)` during
both compilation and linking. Baseline-only executables may remain free of an
OpenMP runtime dependency.

Definition of done:

- A clean build produces baseline and PPM binaries.
- PPM targets use OpenMP correctly.
- Existing target names continue to work.

### Stage 13: Update Documentation and Results

#### `Context.md`

Update the milestone table and deliberate-later-work section to distinguish:

- Fixed PPM `matrix_first` in Milestone 2.
- Dynamic gPPM sequence selection in Milestone 3.

Record the final validation totals and scaling environment after results are
available.

#### `README.md`

Update:

- Project title/status to include Milestone 2.
- Quick-start commands for PPM tests, sweep, and benchmark.
- Repository layout with `ppm.c`, `ppm.h`, and PPM result files.
- A short explanation of partitioning, the required barrier, and fixed
  calculation sequences.
- Figure 3's `35 -> 29` validation.
- Throughput and speedup summary tables after benchmark execution.
- Deliberate omissions so `matrix_first` is not incorrectly described as
  entirely absent until Milestone 3.

#### Results

Keep generated M2 results separate from Milestone 1 artifacts:

```text
results/ppm_sweep_results.csv
results/ppm_benchmark_results.csv
```

Do not overwrite validated Milestone 1 CSV files.

## 6. Per-File Change Summary

| File | Milestone 2 change |
|---|---|
| `src/gf.h` | Declare the operation counter as C11 thread-local storage and update comments. |
| `src/gf.c` | Define the thread-local counter; leave scalar GF arithmetic unchanged. |
| `src/matrix.h` | Declare `mat_mul()`. |
| `src/matrix.c` | Implement uncounted GF matrix multiplication. |
| `src/codec.h` | Declare fixed `ec_decode_matrix_first()`; retain baseline APIs. |
| `src/codec.c` | Implement fixed matrix-first block recovery; do not change `ec_recover()` behavior. |
| `src/ppm.h` | New PPM statistics and SD recovery API. |
| `src/ppm.c` | New validation, SD partitioning, job preparation, OpenMP recovery, barrier, remainder recovery, and count aggregation. |
| `tests/test_ppm.c` | New Figure 3, field, layout, thread-count, and invalid-input tests. |
| `src/ppm_sweep.c` | Preferred new full-grid PPM correctness sweep. |
| `src/benchmark.c` | Add or share benchmark logic for baseline-versus-PPM scaling without changing baseline semantics. |
| `Makefile` | Add OpenMP flags, PPM source dependencies, binaries, and test/sweep/benchmark targets. |
| `Context.md` | Correct the M2/M3 matrix-first boundary and record final M2 results. |
| `README.md` | Document PPM operation, commands, validation, benchmark results, and remaining M3 work. |

`src/sd_code.c` and `src/sd_code.h` should not require algorithm changes. They
already construct the sparse row-local and dense sector-parity structure used
by PPM. Add helpers there only if implementation demonstrates clear reuse that
cannot remain local to `ppm.c`.

## 7. Correctness and Concurrency Risks

### Shared operation counter

A process-global counter creates a C data race. Use thread-local storage and
aggregate per-job values after the parallel loop.

### Early remainder decoding

`Hrest` depends on independently recovered sectors. It must run only after all
independent jobs finish.

### Wrong remainder fault set

`Frest` contains only still-unrecovered dependent faults. Independent faults
must appear as survivors after the barrier.

### Incorrect worker mappings

`ec_decode_normal()` and `ec_decode_matrix_first()` use global sector indices
to address the stripe. Keeping original columns in each `Hi` prevents local
column indices from being mistaken for global indices.

### Partial writes on preparation failure

Prepare and invert subproblems before entering the data-writing phase whenever
possible. Worker errors must be stored and checked after the OpenMP region.

### Zero and one coefficients

Skip zero coefficients. Do not skip coefficient `1`; it still represents one
counted `mult_XORs()` operation.

### Statistics changed by validation

`ec_syndrome_is_zero()` calls `mult_XORs()`. Recovery counts must be captured in
the recovery statistics before syndrome validation runs.

### Oversubscription

Use no more than `p` threads for `p` independent jobs. Record requested and
effective thread counts separately.

### Benchmark fairness

Use the same stripe, failures, timed boundary, warm-up, trial count, compiler
optimization, and validation rules for baseline and PPM.

## 8. Validation Gates

Run gates in this order. Do not proceed to performance claims while an earlier
gate fails.

### Gate A: Baseline regression

```sh
make clean
make test
make run
make sweep
```

Expected: all existing tests pass and Figure 2 remains `C=35`.

### Gate B: Matrix and fixed-sequence correctness

Expected:

- `mat_mul()` known-answer tests pass.
- Normal and fixed matrix-first paths recover identical bytes.
- Their operation counts match the actual coefficient matrices used.

### Gate C: Figure 3

Expected:

```text
baseline C = 35
PPM T=1 C  = 29
p          = 3
bytes      = PASS
syndrome   = PASS
```

### Gate D: Thread-count invariance

Expected for `T=1,2,4,...`:

- Identical recovered bytes.
- Identical checksum.
- Identical operation count.
- Zero syndrome.

### Gate E: Published-grid PPM sweep

Expected:

- Every feasible tested layout recovers correctly.
- No singular submatrices for patterns already correctable by the baseline.
- No internal operation-count mismatch.
- No thread-count-dependent failures.

### Gate F: Scaling benchmark

Expected:

- All benchmark trials validate.
- Baseline, PPM `T=1`, and multi-thread PPM are all recorded.
- Throughput, speedup, and efficiency can be plotted directly from the CSV.

## 9. Definition of Done

Milestone 2 is complete when all of the following are true:

- `ec_recover()` remains available as the unchanged sequential behavior and
  produces the Milestone 1 reference results.
- The operation counter is thread-local and PPM aggregates counts correctly.
- Fixed `matrix_first` decoding exists for PPM independent groups.
- SD failures are partitioned into independent groups and `Hrest` correctly.
- Independent groups execute through OpenMP with no shared writes.
- `Hrest` executes only after the required barrier.
- Figure 3 reproduces `35 -> 29`.
- PPM recovery is byte-identical and has zero syndrome across supported fields,
  layouts, and thread counts.
- The published configuration sweep passes for PPM.
- Baseline and PPM scaling results exist for `T=1,2,4,...`.
- Results include throughput, speedup, and parallel efficiency.
- Documentation clearly separates fixed M2 PPM behavior from dynamic M3 gPPM
  optimization.

## 10. Recommended Implementation Order

1. Run and record baseline regression gates.
2. Convert the counter to thread-local storage.
3. Implement and test `mat_mul()`.
4. Implement and test fixed `ec_decode_matrix_first()`.
5. Add `ppm.h` and serial SD partition preparation.
6. Implement PPM recovery at `T=1`.
7. Pass Figure 3 with `35 -> 29`.
8. Add the OpenMP independent-job loop and barrier.
9. Add thread-count and multi-field tests.
10. Run the full PPM correctness sweep.
11. Add baseline-versus-PPM scaling benchmarks.
12. Record results and update project documentation.

This order isolates algorithmic correctness from concurrency. OpenMP is added
only after serial PPM reproduces the paper's expected partition and operation
count.

# Project Revision Guide: Parallel PPM Recovery for SD Erasure Codes

This document explains the project from first principles. It is intended as a
revision guide for presentation questions, rather than only as implementation
documentation.

## 1. One-Minute Summary

This project reproduces and evaluates part of the paper:

> Shiyi Li, Qiang Cao, Shenggang Wan, Wen Xia, and Changsheng Xie.  
> *gPPM: A Generalized Matrix Operation and Parallel Algorithm to Accelerate
> the Encoding/Decoding Process of Erasure Codes.*  
> ACM Transactions on Architecture and Code Optimization, 2023.

Erasure codes protect stored data against disk and sector failures. Recovering
lost data requires finite-field matrix operations over large data blocks. The
traditional decoder treats every failed sector as one large coupled problem and
processes it sequentially.

This project first implements that traditional decoder as a validated
sequential baseline. It then implements the paper's PPM idea for SD codes:

1. Identify groups of failed sectors that can be recovered independently.
2. Partition the large parity-check matrix into smaller submatrices.
3. Recover independent groups concurrently with OpenMP.
4. Wait for those groups to finish.
5. Recover the dependent remainder using the newly recovered sectors.

PPM also changes the multiplication order for independent submatrices. This is
mathematically equivalent but can require fewer expensive operations over large
data regions.

The implementation reproduces the paper's small example, reducing the counted
work from `35` to `29` region operations. Across the benchmark configurations,
the measured median speedup reaches `2.33x` at eight OpenMP threads.

## 2. What Problem Is Being Solved?

### 2.1 Storage devices fail in different ways

A storage system can experience:

- Complete disk failures.
- Individual unreadable or corrupted sectors.
- Multiple disk and sector failures at the same time.

Simply storing extra full copies is expensive. Erasure codes generate parity
data so that missing data can be reconstructed with less storage overhead than
full replication.

### 2.2 Traditional erasure decoding is computationally expensive

The code represents the relationship between sectors with a parity-check
matrix `H`. If `B` is the vector of all sectors in a stripe, a valid stripe
satisfies:

```text
H * B = 0
```

When some sectors fail, split the columns of `H` into:

- `F`: columns corresponding to faulty sectors.
- `S`: columns corresponding to surviving sectors.
- `BF`: the unknown faulty sector data.
- `BS`: the known surviving sector data.

Then:

```text
F * BF + S * BS = 0
BF = F^-1 * S * BS
```

In a binary extension field, subtraction and addition are both XOR, so no
separate minus sign is required in the implementation.

The matrix coefficients are small finite-field values, but the vector entries
are whole sectors. One matrix coefficient can therefore trigger an operation
over kilobytes or megabytes of data. These large-region operations dominate the
runtime.

### 2.3 The research question

The central question is:

> Can we recover the same data correctly while performing fewer expensive
> whole-region operations and executing independent work on multiple CPU
> cores?

PPM answers yes for asymmetric parity codes such as SD codes.

## 3. What Is an SD Code?

The project uses SD codes, designed to tolerate complete disk failures plus
additional sector failures.

An SD code is written as:

```text
SD^{m,s}_{n,r}(w | a0, ..., a(m+s-1))
```

The parameters are:

| Symbol | Meaning |
|---|---|
| `n` | Total number of disks or strips in a stripe |
| `r` | Number of sector rows per strip |
| `m` | Number of complete disk failures tolerated |
| `s` | Number of additional sector failures tolerated |
| `w` | Finite-field word size: 8, 16, or 32 bits |
| `a_i` | Published coding coefficients |
| `z` | Number of physical stripe rows containing the `s` additional failures |

The stripe contains `n*r` sectors. Its parity-check matrix has:

```text
rows(H) = m*r + s
cols(H) = n*r
```

### 3.1 Why SD is called an asymmetric parity code

`H` contains two different equation families:

1. **Row-local disk-parity equations:** `m` equations for each physical stripe
   row. These equations contain nonzero coefficients only for sectors in that
   row.
2. **Dense sector-parity equations:** `s` equations spanning the entire stripe.

The parity equations do not all cover the same amount of data. That asymmetric
structure is exactly what PPM exploits.

## 4. Finite-Field Arithmetic

All coding arithmetic is performed over `GF(2^w)` rather than ordinary integer
arithmetic.

The project supports:

- `GF(2^8)` using log and antilog tables.
- `GF(2^16)` using log and antilog tables.
- `GF(2^32)` using scalar split tables because a full `2^32` log table would be
  impractical.

Addition in these fields is bitwise XOR. Multiplication uses the field's
primitive polynomial and lookup structures.

The hot operation is:

```c
mult_XORs(source_region, destination_region, coefficient, bytes, gf)
```

It multiplies every field symbol in a source region by one coefficient and
XORs the result into a destination region.

## 5. How Computational Work Is Measured

### 5.1 `u(M)`

`u(M)` is the number of nonzero entries in matrix `M`.

When a matrix is multiplied by a vector of data blocks, every nonzero entry
causes one `mult_XORs()` call. Therefore, `u(M)` is a useful algorithmic work
metric.

Zero coefficients are skipped. Coefficient `1` is still counted because it is
a nonzero matrix entry, although internally it can use a plain XOR.

### 5.2 `v(M)` and `c`

The paper's more detailed gPPM cost model is:

```text
C = u + c*v
```

Here:

- `u` is the number of nonzero entries.
- `v` is the number of entries greater than `1`.
- `c` represents the extra relative cost of multiplication compared with a
  coefficient-one XOR.

The current PPM milestone validates `u` by counting `mult_XORs()` calls. Dynamic
selection based on `u + c*v` belongs to later generalized gPPM work.

### 5.3 Throughput, speedup, and efficiency

The performance benchmark also records:

```text
Throughput = useful data MiB / elapsed seconds
Speedup(T) = baseline time / PPM time at T threads
Efficiency(T) = Speedup(T) / T
```

Operation count explains algorithmic work. Throughput and speedup measure real
execution performance, including allocation, matrix preparation, OpenMP
overhead, memory bandwidth, and the scalar finite-field implementation.

## 6. Milestone 1: Sequential Baseline

Milestone 1 implements the traditional normal calculation sequence:

```text
T  = S * BS
BF = F^-1 * T
```

The counted cost is:

```text
C_normal = u(S) + u(F^-1)
```

The recovery path is:

```text
ec_recover()
    ec_split()
    mat_invert()
    ec_decode_normal()
```

The baseline is deliberately:

- Sequential.
- Scalar, without SIMD.
- Normal-sequence only.
- Kept available as the control implementation for all PPM comparisons.

### 6.1 Why establish a baseline first?

A trustworthy optimization requires a reference that is already correct. The
baseline establishes:

- Correct construction of `H`.
- Correct GF arithmetic.
- Correct encoding and recovery.
- A known operation count.
- A throughput reference measured on the same machine and workload.

Without this baseline, a faster result could be caused by doing less work
incorrectly.

## 7. The Paper's First Insight: Multiplication Order Matters

The required result is:

```text
BF = F^-1 * S * BS
```

Matrix multiplication is associative, so it can be evaluated in two orders.

### Normal sequence

```text
BF = F^-1 * (S * BS)
Cost = u(S) + u(F^-1)
```

### Matrix-first sequence

```text
G  = F^-1 * S
BF = G * BS
Cost = u(G) = u(F^-1 * S)
```

The multiplication `F^-1*S` operates only on small coefficient matrices. The
paper excludes that small matrix-by-matrix work from `C`, because it is tiny
compared with processing large sector regions.

Finite-field terms can cancel while calculating `F^-1*S`, creating zero
entries in `G`. Those zero entries avoid later region operations. Therefore,
matrix-first can perform fewer expensive block operations while producing
exactly the same recovered data.

The project adds `mat_mul()` for the small coefficient multiplication and
`ec_decode_matrix_first()` for the resulting block recovery.

## 8. The Paper's Second Insight: Some Failures Are Independent

### 8.1 Independent sectors

For each physical stripe row, count its failed sectors.

If a row has exactly `m` failed sectors, its `m` row-local parity equations are
enough to solve those `m` unknowns. These equations do not involve sectors from
another physical row.

That failure group is independent because it can be recovered using:

- Its own `m` row-local equations.
- Surviving sectors in the same physical row.
- No result produced by another independent group.

### 8.2 Dependent sectors

If a row contains more than `m` failures, its `m` local equations are not enough
to solve all unknowns in that row. Recovery must use the `s` dense equations
that span the full stripe.

These failures are dependent because the dense equations couple data from
multiple rows. In the PPM sequence, the remainder also reads the sectors that
were just recovered by independent jobs.

### 8.3 What are dependent sectors dependent on?

They depend on:

- The global dense sector-parity equations.
- Other dependent failures solved in the same remainder system.
- The completed values of independently recovered sectors, which become
  surviving inputs to the remainder.

This is why the remainder cannot execute concurrently with the independent
jobs.

## 9. Milestone 2: PPM

The PPM recovery algorithm is:

```text
1. Count failures in every physical stripe row.
2. For rows with exactly m failures, create H0 ... H(p-1).
3. Put all remaining local rows and all dense rows into Hrest.
4. Prepare and invert all subproblems.
5. Recover H0 ... H(p-1) in an OpenMP parallel loop using matrix-first.
6. Wait at the loop's implicit barrier.
7. Recover Hrest sequentially using the normal sequence.
```

For the deterministic SD layouts used here:

```text
p = r - z
```

where `p` is the number of independent groups.

### 9.1 Why fixed matrix-first is part of PPM

The paper's PPM algorithm prescribes matrix-first for independent submatrices.
Threading alone would provide concurrency, but it would not reproduce the
paper's algorithmic operation-count reduction.

The milestone boundary is therefore:

```text
Milestone 2 PPM:
    independent submatrices -> fixed matrix-first
    Hrest                  -> fixed normal sequence

Later gPPM:
    calculate costs and dynamically choose the sequence
```

### 9.2 Why is there a barrier?

Each independent job writes a disjoint set of sectors, so those jobs do not
need locks around the data.

However, `Hrest` includes dense equations that read the independently recovered
sectors. It must not start until every independent job has completed. The
implicit barrier at the end of the OpenMP `for` loop enforces this dependency.

### 9.3 Thread-safe instrumentation

The original operation counter was one global variable. Concurrent increments
would create a data race, and using an atomic increment in every hot operation
would add contention to the benchmark.

The PPM implementation uses C11 thread-local storage:

```c
_Thread_local uint64_t gf_mult_xors_count;
```

Each worker records its own job count. The parent sums the job counts after the
barrier.

## 10. Figure 2 and Figure 3 Example

The paper's example uses:

```text
SD^{1,1}_{4,4}(8 | 1,2)
faults = {2, 6, 10, 13, 14}
```

The traditional decoder recovers all five failures together:

```text
u(S) = 22
u(F^-1) = 13
C = 35
```

PPM partitions the failures into:

```text
H0 -> {2}
H1 -> {6}
H2 -> {10}
Hrest -> {13, 14}
```

The first three sectors are independent. Sectors `13` and `14` are dependent
and use the dense equation in the remainder.

The implemented PPM path produces:

```text
Baseline C = 35
PPM C      = 29
Reduction  = 17.1%
```

Both paths recover byte-for-byte identical data and produce a zero syndrome.

## 11. Where Does the Data Come From?

There are three different kinds of data in the project. They should not be
confused.

### 11.1 Published FAST coefficient table

`data/FAST-Coefficients.txt` contains the coding parameters used to construct
valid SD parity-check matrices.

The table comes from James S. Plank's published FAST SD encoder/decoder work,
which is also referenced by the selected paper. It covers:

```text
n = 4 ... 24
r = 4 ... 24
m = 1 ... 3
s = 1 ... 3
```

The number of configurations is:

```text
21 values of n * 21 values of r * 3 values of m * 3 values of s
= 3,969 configurations
```

Each line contains:

```text
n m s r w a0 a1 ... a(m+s-1)
```

The loader checks that:

- All 3,969 expected combinations are present.
- There are no duplicate combinations.
- `w` is 8, 16, or 32.
- Every required coefficient is nonzero and fits the selected field.
- There is no trailing malformed data.

This coefficient file is not a machine-learning dataset and does not contain
user or production storage data. It is a published parameter grid for SD-code
construction.

### 11.2 Synthetic sector contents

The bytes stored in benchmark sectors are generated locally with a
deterministic pseudo-random generator. Determinism makes runs reproducible and
allows exact byte comparison after recovery.

The sector contents do not need to represent real files. Erasure-code
correctness must hold for arbitrary bytes.

### 11.3 Synthetic failure layouts

`sd_failure_sectors()` constructs a deterministic representative worst-case
layout:

- The leftmost `m` complete disks fail.
- The `s` additional sector failures are distributed across exactly `z` rows.

The sweep covers every published parameter combination and every applicable
`z`, but it does not enumerate every possible combinatorial placement of failed
disks and sectors.

There are 7,938 `(configuration, z)` points in total. Of these, 105 are
geometrically infeasible, leaving 7,833 evaluated layouts.

### 11.4 Generated result datasets

The files under `results/` are produced locally by this implementation:

| File | Meaning |
|---|---|
| `sweep_results.csv` | Sequential correctness and operation counts |
| `benchmark_results.csv` | Sequential encoding/decoding timings |
| `ppm_sweep_results.csv` | PPM correctness and thread-count invariance |
| `ppm_benchmark_results.csv` | Baseline-versus-PPM scaling trials |

These are experimental outputs, not external source datasets.

## 12. Experimental Methodology

### 12.1 Correctness sweep

The correctness sweep uses small 16-byte sectors because its goal is to verify
many matrix configurations quickly, not measure throughput.

For each feasible point it:

1. Builds `H` from the published coefficients.
2. Encodes deterministic data.
3. Checks the encoded syndrome.
4. Saves a golden copy.
5. Wipes the selected failed sectors.
6. Recovers the sectors.
7. Compares every recovered byte with the golden copy.
8. Checks the syndrome again.
9. Validates operation counts.

The PPM sweep repeats recovery at `T=1` and `T=4`, requiring identical data and
identical operation counts. Threading should change elapsed time, not the
algorithm's mathematical work or result.

### 12.2 Performance benchmark

The PPM performance benchmark uses:

- `n=16` and `r=16`.
- `z=1`.
- All nine `m,s in {1,2,3}` combinations.
- An exact 32 MiB codeword.
- One warm-up per implementation/thread setting.
- Ten measured trials.
- Baseline plus PPM at `T=1,2,4,8`.

The timed interval includes the full recovery call, including partitioning,
allocation, splitting, inversion, and data recovery.

The following are deliberately outside the timed interval:

- Fault injection.
- Byte comparison.
- Syndrome validation.
- Checksum calculation.
- CSV output.

This prevents validation and file I/O from contaminating decoder throughput.

### 12.3 Why include PPM at `T=1`?

`T=1` separates two sources of improvement:

```text
Baseline -> PPM T=1
    algorithmic gain from partitioning and matrix-first

PPM T=1 -> PPM T=N
    parallel gain from multiple workers
```

Without the `T=1` result, it would be difficult to tell whether improvement
came from reduced work or threading.

## 13. Correctness Results

### Sequential baseline

| Measure | Result |
|---|---:|
| Published configurations | 3,969 |
| Total `(configuration, z)` points | 7,938 |
| Geometrically infeasible points | 105 |
| Feasible points evaluated | 7,833 |
| Encode/recovery passes | 7,833 |
| Singular recovery matrices | 0 |
| Internal count mismatches | 0 |
| Measured counts above the formula | 0 |

### PPM

| Measure | Result |
|---|---:|
| Feasible points at `T=1` and `T=4` | 7,833 |
| Byte-comparison failures | 0 |
| Syndrome failures | 0 |
| Thread-count operation mismatches | 0 |
| Figure 3 baseline count | 35 |
| Figure 3 PPM count | 29 |

### Why are some sequential counts below the formula?

The closed-form `C1` expression assumes a generic nonzero pattern. With actual
published coefficients, entries in `F^-1` can cancel to zero. This reduces the
measured operation count.

Therefore:

- A count below the generic formula can be valid coefficient cancellation.
- A count above the formula would indicate a bug.
- The implementation also checks the exact identity
  `measured = u(S) + u(F^-1)` for the actual matrices.

## 14. Performance Results

All 450 measured PPM benchmark trials passed byte, syndrome, checksum, count,
and metadata validation.

Median speedups over the sequential baseline were:

| m | s | T=1 | T=2 | T=4 | T=8 |
|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 1.13x | 1.31x | 1.34x | 1.41x |
| 1 | 2 | 1.03x | 1.10x | 1.18x | 1.22x |
| 1 | 3 | 1.04x | 1.14x | 1.20x | 1.18x |
| 2 | 1 | 1.02x | 1.25x | 1.65x | 1.77x |
| 2 | 2 | 1.08x | 1.35x | 1.48x | 1.46x |
| 2 | 3 | 1.17x | 1.36x | 1.39x | 1.49x |
| 3 | 1 | 1.19x | 1.66x | 2.10x | 2.33x |
| 3 | 2 | 1.19x | 1.53x | 1.80x | 1.67x |
| 3 | 3 | 1.24x | 1.53x | 1.64x | 1.73x |

The important observations are:

- Every configuration improves at `T=1`, confirming an algorithmic benefit.
- Larger `m` generally exposes more useful work for parallel execution.
- Speedup is not linear in thread count.
- Some configurations peak before eight threads because of thread overhead,
  limited independent work, memory bandwidth, and the sequential remainder.
- The best measured median speedup is `2.33x`.

The recorded Milestone 1 environment was an Intel Core Ultra 9 185H with GCC
11.4.0 at `-O2`. Absolute numbers should not be compared directly with the
paper's machines because the processors and arithmetic backends differ.

## 15. Why Speedup Is Not Linear

Several effects limit scaling:

### Sequential remainder

`Hrest` runs after the barrier and remains sequential. Amdahl's law limits total
speedup when part of the workload cannot be parallelized.

### Limited number of independent groups

The maximum useful worker count is `p`, not an arbitrary number of threads. For
the benchmark, `r=16` and `z=1`, so `p=15`.

### Threading overhead

Starting and coordinating workers has a cost. Small independent tasks may not
contain enough data work to amortize that cost.

### Memory bandwidth

Every worker scans large sector regions. Additional cores eventually compete
for shared memory bandwidth and cache capacity.

### Different finite fields

GF(2^8), GF(2^16), and GF(2^32) use different arithmetic paths and have
different costs. This affects both absolute throughput and scaling behavior.

## 16. Code Architecture

| File | Responsibility |
|---|---|
| `src/gf.c`, `src/gf.h` | GF arithmetic, `mult_XORs()`, thread-local counting |
| `src/matrix.c`, `src/matrix.h` | Matrix allocation, inversion, multiplication, nonzero counts |
| `src/sd_code.c`, `src/sd_code.h` | SD parity-check matrix and failure/parity layouts |
| `src/codec.c`, `src/codec.h` | Split, normal decode, matrix-first decode, baseline recovery |
| `src/ppm.c`, `src/ppm.h` | SD partitioning, OpenMP jobs, barrier, remainder recovery |
| `src/sweep.c` | Full sequential correctness sweep |
| `src/ppm_sweep.c` | Full PPM correctness and thread-invariance sweep |
| `src/benchmark.c` | Sequential 32 MiB benchmark |
| `src/ppm_benchmark.c` | Baseline-versus-PPM scaling benchmark |
| `tests/test_ppm.c` | Figure 3, fields, threads, and invalid-input tests |
| `scripts/plot_milestone2.py` | Presentation plot generator |

## 17. How to Run the Project

```sh
make                # build baseline and PPM executables
make test           # run all focused tests
make run            # run the Figure 2 baseline example
make sweep          # sequential full-grid validation
make ppm-sweep      # PPM full-grid validation
make benchmark      # sequential 32 MiB benchmark
make ppm-benchmark  # baseline-versus-PPM scaling benchmark
make plots          # generate PNG and SVG presentation charts
```

Presentation plots are written to `results/plots/`.

## 18. What Has Not Been Implemented Yet?

The current scope is SD-specific PPM with OpenMP. It does not yet include:

- Dynamic sequence selection using `C = u + c*v`.
- Generalized PPM for symmetric parity codes such as Reed-Solomon.
- SIMD/SSE/AVX region arithmetic.
- CUDA or MPI implementations.
- Exhaustive enumeration of every possible failure placement.
- Real storage-device I/O or network transfer measurements.
- Energy or power measurements.

These are limitations, not hidden failures. The implementation isolates the
partitioning and shared-memory parallelism contribution first.

## 19. Likely Presentation Questions

### What is the main contribution of this project?

It reproduces a correct traditional SD decoder, implements the paper's PPM
partitioning and OpenMP recovery, validates it over the complete published
parameter grid, and measures algorithmic and parallel speedup separately.

### Is decoding the same as recovery here?

Yes. Erasure decoding reconstructs lost sectors from surviving data and parity.
The code uses `recover` in function names and `decode` in algorithm names.

### Why can encoding use the same recovery code?

Encoding treats parity sectors as the unknown sectors. Solving `H*B=0` for
those unknown parity sectors is the same matrix-decoding problem.

### Why are some sectors independent?

Their physical stripe row has exactly `m` failures and exactly `m` row-local
equations. Those equations solve the failures without equations or recovered
data from another row.

### What makes the remainder dependent?

Rows with extra failures need dense parity equations spanning the full stripe.
The remainder also uses independent sectors after they have been recovered.

### Why does matrix-first produce the same answer?

Matrix multiplication is associative:

```text
F^-1 * (S * BS) = (F^-1 * S) * BS
```

Only the multiplication order changes, not the linear transformation.

### Why can matrix-first be faster?

Multiplying the small matrices first can create zero coefficients through
finite-field cancellation. Those zeros eliminate expensive operations over
large data regions.

### Why was matrix multiplication not in Milestone 1?

The traditional normal sequence does not calculate `F^-1*S`; it multiplies `S`
and `F^-1` separately against block vectors. Small matrix multiplication is
needed only when matrix-first is introduced.

### Why not simply parallelize the original full matrix?

The full system does not expose the same clean independent data ownership. PPM
uses the asymmetric structure to create subproblems with disjoint outputs and
explicitly handles the dependency on the remainder.

### Why not use an atomic counter?

An atomic increment on every region operation would add shared contention to
the hot path being benchmarked. Thread-local counters avoid the race without
serializing workers.

### Why is there an implicit barrier?

The remainder reads sectors recovered by the independent jobs. Starting it
early could read missing or partially written data.

### Why benchmark `T=1`?

It measures the benefit of partitioning and matrix-first without parallelism.
Comparing higher thread counts with `T=1` then isolates the threading benefit.

### Why are only nine configurations used for throughput?

The benchmark fixes `n=r=16` and tests all nine `m,s in {1,2,3}` combinations
to keep the 32 MiB experiment controlled while covering all supported failure
tolerance levels and field widths. The correctness sweep covers the full grid.

### Is the sweep exhaustive?

It is exhaustive over the published parameter grid and applicable `z` values,
but it uses one deterministic representative failure placement per point. It is
not exhaustive over every combinatorial failure location.

### Why use synthetic bytes instead of real files?

The decoder is content-independent. Deterministic pseudo-random bytes exercise
arbitrary symbol values, make exact comparison possible, and remove file-system
I/O from the experiment.

### What does a zero syndrome prove?

It proves the recovered stripe satisfies every parity equation in `H`. Byte
comparison additionally proves it exactly matches the original encoded stripe.

### Why use both byte comparison and syndrome checking?

They test different properties. A zero syndrome checks code consistency; byte
comparison checks exact recovery of the original data. Using both is stronger
than either alone.

### Why can the measured baseline count be below `C1`?

The formula assumes a generic nonzero pattern. Published coefficients can
cancel entries of `F^-1` to zero, reducing real work. The implementation checks
the exact matrices as well as the upper-bound formula.

### Why does eight threads sometimes perform worse than four?

More threads increase scheduling and synchronization overhead and compete for
memory bandwidth. The workload also contains a sequential remainder, so more
threads do not guarantee better total performance.

### How is this different from gPPM?

PPM uses a fixed strategy for asymmetric codes: matrix-first for independent
submatrices and normal for the remainder. gPPM dynamically evaluates candidate
sequences with a refined cost model and generalizes the idea to other code
families.

### What would you improve next?

The next steps would be dynamic sequence selection, SIMD region arithmetic,
comparison with symmetric codes such as Reed-Solomon, deeper profiling, and
testing more failure placements and hardware platforms.

## 20. Suggested Presentation Narrative

A clear presentation order is:

1. Storage must survive disk and sector failures.
2. SD codes provide that protection but traditional matrix decoding is serial
   and performs many expensive whole-region operations.
3. Establish the validated sequential baseline.
4. Explain the two PPM observations: independent failures and beneficial
   multiplication order.
5. Show the partitioned OpenMP algorithm and required barrier.
6. Show Figure 3: `35 -> 29` operations.
7. Show `T=1` results as algorithmic improvement.
8. Show `T=2,4,8` scaling and the `2.33x` maximum speedup.
9. Explain non-linear scaling with the sequential remainder, overhead, and
   memory bandwidth.
10. End with correctness evidence, limitations, and future gPPM/SIMD work.

## 21. Sources and Provenance

Primary project sources are:

- The selected gPPM paper in `docs/pc project paper.pdf`.
- Plank's published FAST SD coefficient table in
  `data/FAST-Coefficients.txt`.
- Plank's open-source SD encoder/decoder work, cited as reference 40 in the
  selected paper.
- The SD-code construction paper by Plank, Blaum, and Hafner, cited as
  reference 41 in the selected paper.
- Locally generated correctness and benchmark results under `results/`.

The implementation, synthetic workloads, experiments, and result CSVs are
contained in this repository and can be reproduced with the Makefile commands
listed above.

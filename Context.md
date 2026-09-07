# Project Context

Selected paper:

> Shiyi Li, Qiang Cao, Shenggang Wan, Wen Xia, and Changsheng Xie.
> "gPPM: A Generalized Matrix Operation and Parallel Algorithm to Accelerate
> the Encoding/Decoding Process of Erasure Codes." ACM TACO 20(4), 2023.

The source paper is stored as `docs/pc project paper.pdf`. The standalone
Milestone 1 report is `docs/Milestone_1_Report.pdf`;
`docs/finalfi_with_baseline_results.pdf` preserves the original literature
review and appends the validated baseline results.

## Evaluation milestones

| Milestone | Required work |
|---|---|
| 1 | Literature review, target metrics, and a sequential C/Python baseline |
| 2 | Initial OpenMP/CUDA/MPI implementation and scaling comparison |
| 3 | Advanced optimization such as communication hiding, tiling, vectorization, or hybrid scaling |
| 4 | Profiling, final benchmarks, and research paper |

## Milestone 1 baseline

The implemented baseline is the traditional SD-code parity-check-matrix method
from paper Section 2.2. It is deliberately single-threaded, scalar, and uses only
the normal calculation sequence:

```text
H * B = 0
F = faulty columns of H
S = surviving columns of H
T = S * BS
BF = F^-1 * T
```

Encoding uses the same path, treating the designated parity sectors as the
unknown set. GF(2^8), GF(2^16), and GF(2^32) are supported. The matrix
construction and coefficients match Plank's published FAST SD implementation.

## Metrics

Two baseline metrics are recorded:

1. `C`, the number of whole-region `mult_XORs()` operations in Step 4.
2. Useful-data throughput in MiB/s for an exact 32 MiB codeword.

For the normal sequence:

```text
C = u(S) + u(F^-1)
C1 = n*r*(m+s) + m*(m*r+s)*(z-1) + m^2*(r-z)
```

`C1` is a generic-position upper bound. Published coefficient values can cause
entries of `F^-1` to cancel to zero, making measured `C` smaller. A measured
value greater than `C1`, or a mismatch between `C` and `u(S)+u(F^-1)`, is an
implementation failure.

## Correctness results

Run with `make test`, `make run`, and `make sweep`.

| Measure | Result |
|---|---:|
| Published SD configurations | 3,969 |
| Total `(configuration, z)` points | 7,938 |
| Geometrically infeasible points | 105 |
| Feasible points evaluated | 7,833 |
| Encode and round-trip passes | 7,833 |
| Singular matrices | 0 |
| Internal count mismatches | 0 |
| Measured counts above C1 | 0 |
| Exact C1 matches | 7,649 |
| Benign counts below C1 | 184 |

Each feasible `(configuration, z)` point uses one deterministic representative
failure layout: the leftmost `m` disks and `s` additional sectors distributed
over exactly `z` rows. The sweep covers the complete parameter grid, but it does
not enumerate every combinatorial placement of disk and sector failures.

The Figure 2 smoke test also reproduces `u(S)=22`, `u(F^-1)=13`, and
`C=35=C1`, with zero syndrome after encoding and recovery.

Full per-point results are in `results/sweep_results.csv`.

## Benchmark results

Run with `make benchmark`. The suite uses `n=16`, `r=16`, `z=1`, all nine
`m,s in {1,2,3}` pairs, an exact 32 MiB codeword, one warm-up, and ten measured
trials for both encoding and decoding. Data generation, fault injection,
validation, checksums, and output are outside the timed region.

The recorded environment is an Intel Core Ultra 9 185H with GCC 11.4.0 `-O2`.
The executable is single-threaded and has no SIMD. All 180 measured trials pass.
The complete raw data is in `results/benchmark_results.csv`; the mean throughput
table is in `README.md` and both Milestone 1 reports.

## Deliberate later work

The following features are not part of the sequential baseline:

| Feature | Milestone |
|---|---|
| PPM log table and matrix partitioning | 2 |
| OpenMP recovery of independent sub-matrices | 2 |
| Parallel-versus-sequential scaling results | 2 |
| Matrix-first sequence and dynamic sequence selection | 3 |
| SIMD/SSE/AVX region arithmetic | 3 |
| RS construction for generalized comparison | 3 |
| Profiling and final bottleneck analysis | 4 |

## Main artifacts

| Artifact | Purpose |
|---|---|
| `src/main.c` | Figure 2 smoke test |
| `src/sweep.c` | Full SD correctness and operation-count sweep |
| `src/benchmark.c` | Sequential 32 MiB throughput benchmark |
| `data/FAST-Coefficients.txt` | Published SD coefficient grid |
| `results/sweep_results.csv` | Per-point correctness results |
| `results/benchmark_results.csv` | Per-trial timing and validation results |
| `docs/Milestone_1_Report.pdf` | Polished standalone Milestone 1 report |
| `docs/Milestone_1_Report.docx` | Editable source for the standalone report |
| `docs/finalfi_with_baseline_results.pdf` | Original literature review with appended baseline results |
| `docs/finalfi_with_baseline_results.docx` | Editable source for the appended report |

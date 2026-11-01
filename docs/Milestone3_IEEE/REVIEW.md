# Adversarial review of the final paper

Reviewed 1 November 2026 by an independent AI reviewer, with a second pass
after corrections. This is an internal consistency audit, not formal peer
review or a fresh performance reproduction. The reviewer inspected paper
claims against implementation, tests, and retained per-trial CSVs; the main
agent separately checked presentation, references, and correctness reruns.

## Confirmed findings and corrections

| Finding | Correction in the paper |
|---|---|
| Stale claim that `(m,s)=(3,1)` slowed by 6% with the parallel remainder at four threads | Replaced with the retained CSV result: serial 5.449058 ms, parallel 4.111660 ms, a 1.325269× gain |
| Eight-thread results obscured faster four-thread CPU results | Reports six L3 workloads becoming 36.99%–66.23% slower from four to eight threads |
| “GPU beats optimized CPU” suggested superiority to a tuned CPU | Abstract, introduction, table and conclusion explicitly restrict GPU gains to the fixed eight-thread CPU comparator; four-thread comparison in the same run remains unavailable |
| Operation-count checks overstated as independent execution validation | Distinguishes call instrumentation from matrix-derived logical counts and warm-up/cross-thread consistency |
| GPU scalar/table comparison could be read as isolated kernel acceleration | Identifies full-decoder comparisons, including backend-specific calibration and selected sequence/work changes |
| Calibration presented too generally | States the fixed-coefficient/restricted-input probe is a heuristic |
| Workload insufficiently specified | Adds deterministic failure positions, 15 independent groups, 16-byte correctness-sweep sectors, and field/failure-tolerance coupling |
| Speedup headings `L1/L0` etc. could mean the inverse time ratio | Replaced with explicit “L1 speedup,” “L2 speedup,” and “L3 speedup”; caption defines the time ratio |
| Sanitizer success lacked retained output | Reran the test successfully and retained the transcript under `results/milestone_gpu/` |

## Numerical checks reproduced by the reviewer

- CPU-layer L3 speedups: **18.3714, 6.0662, 1.6892, 25.4162, 5.9380,
  1.8476, 34.8472, 6.6126, 1.9450×**, in the paper's configuration order.
- GPU table versus eight-thread CPU, GF(2^32): **1.317072, 1.335812,
  1.385710×**. All 27 CPU/GPU table medians match the retained raw data.
- CPU trials: **270 scalar + 450 SIMD = 720 PASS**.
- CPU/GPU comparison: **270 PASS**.
- CPU sweep: **7,833 PASS + 105 SKIP = 7,938**, with equal T=1/T=4 counts.
- Focused GPU test source supports **12 matrix checks and 64 SD recoveries**.

After scientific corrections, the follow-up reviewer found no remaining major
scientific issue. Its remaining table-label correction has been applied.
Compute Sanitizer and all existing CPU tests were subsequently rerun and passed.

## Independent bibliography and presentation checks

- Names and affiliation match the user-provided author list.
- Uses IEEEtran conference layout and Times-compatible Termes text; embedded
  fonts and page layout are checked on the rebuilt PDF.
- gPPM reference is supported by the repository's original ACM paper and DOI
  `10.1145/3625005`.
- PPM metadata was checked against Crossref: ICPP 2015, pp. 460–469,
  DOI `10.1109/ICPP.2015.55`.
- SIMD paper title/authors/venue were checked against Plank's official page:
  <https://web.eecs.utk.edu/~jplank/plank/papers/FAST-2013-GF.html>.
- The USENIX page for the original FAST SD article says it was removed because
  of a dispute over its contents. It is no longer used as supporting evidence
  in this paper. SD recovery background is supported by the gPPM paper, and
  coefficient/software provenance cites the available technical report:
  <https://web.eecs.utk.edu/~jplank/plank/papers/CS-13-704.html>.

## Remaining experimental limits

The study uses one machine and a fixed stripe size; field width changes with
failure tolerance. Variants run in fixed order without confidence intervals or
randomized repetition across sessions. The GPU comparator is not a per-workload
thread-tuned CPU. Calibration is heuristic, and logical operation counts are
not an independent instruction-level oracle. Tests cover representative layouts,
not all erasure placements. These limits are stated in the paper and constrain
its claims; the audit does not replace broader benchmarking or external review.

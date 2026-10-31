# Milestone 3 Results: Parallelizing the Remainder

## Experiment

The M3 decoder splits the normal-sequence dependent remainder into disjoint
symbol-aligned byte ranges. Independent PPM groups are held at the same thread
count while the remainder is run either serially or with the same number of
threads. This isolates the effect of remainder parallelism.

- **Workload:** SD `(n,r)=(16,16)`, `z=1`, 32 MiB stripe, nine `(m,s)` settings,
  with GF(2^8), GF(2^16), and GF(2^32).
- **Platform/build:** Intel Core Ultra 9 185H, GCC 11.4.0, `-O2 -fopenmp`.
- **Threads:** 4 and 8 for independent groups; remainder serial (`R=1`) or
  parallel (`R=T`).
- **Timing:** 10 trials per case; table reports median recovery time in ms.
- **Reference:** M3 whole-matrix normal decode, using the same M3 SIMD-enabled
  arithmetic. Results and per-trial validation are in
  [`../results/m3_benchmark_results.csv`](../results/m3_benchmark_results.csv).

## SIMD kernel output

The arithmetic-kernel benchmark compared the scalar reference with the selected
SIMD path using 10 trials. Each trial applied the operation 256 times to a
128 KiB region (32 MiB total processed), and required byte-identical output.
Times are median milliseconds for that 32 MiB of region work.

| Field | Scalar | SIMD | Speedup |
|---|---:|---:|---:|
| GF(2^8), SSSE3 | 23.093 ms | 1.210 ms | 19.09× |
| GF(2^16), AVX2 | 16.863 ms | 5.344 ms | 3.16× |
| GF(2^32), AVX2 | 9.661 ms | 8.201 ms | 1.18× |

These are region-kernel measurements, not end-to-end decoder speedups. Raw
results are in
[`../results/m3_simd_benchmark_results.csv`](../results/m3_simd_benchmark_results.csv).

## Measured output

| m | s | Field | Normal | T=4 serial remainder | T=4 parallel remainder | T=4 remainder gain | T=8 serial remainder | T=8 parallel remainder | T=8 remainder gain |
|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1 | GF(2^8) | 4.272 | 3.560 | 2.180 | 1.63× | 3.585 | 1.685 | 2.13× |
| 1 | 2 | GF(2^16) | 16.140 | 13.535 | 7.012 | 1.93× | 14.383 | 8.636 | 1.67× |
| 1 | 3 | GF(2^32) | 35.170 | 33.964 | 20.376 | 1.67× | 34.239 | 25.060 | 1.37× |
| 2 | 1 | GF(2^8) | 8.135 | 4.543 | 2.852 | 1.59× | 4.323 | 2.593 | 1.67× |
| 2 | 2 | GF(2^16) | 24.200 | 18.220 | 8.668 | 2.10× | 20.601 | 13.237 | 1.56× |
| 2 | 3 | GF(2^32) | 48.494 | 39.156 | 25.499 | 1.54× | 40.621 | 31.293 | 1.30× |
| 3 | 1 | GF(2^8) | 9.991 | 5.055 | 5.402 | 0.94× | 4.422 | 2.940 | 1.50× |
| 3 | 2 | GF(2^16) | 32.675 | 20.474 | 11.104 | 1.84× | 24.925 | 17.345 | 1.44× |
| 3 | 3 | GF(2^32) | 62.566 | 43.117 | 28.891 | 1.49× | 52.094 | 37.566 | 1.39× |

“Remainder gain” is serial-remainder median divided by parallel-remainder
median at the same T. At T=4, parallel remainder improved eight configurations
and was 6% slower for `(m,s)=(3,1)`; gains ranged from **1.49× to 2.10×** in the
improved cases. At T=8, all nine improved, by **1.30×–2.13×**. The T=4
regression shows the added parallel-region overhead can outweigh the saved work
for some cases. In all trials the selector chose the partitioned path. Across
baseline and gPPM variants, all **450/450** trials passed byte comparison,
checksum, and syndrome validation.

## Correctness sweep

`make m3-sweep` evaluated all **7,833 feasible** published SD parameter/layout
points (105 additional points were infeasible): **7,833 passed, 0 failed**.
The sweep compares one-thread and parallel-thread runs for output and operation
count consistency. Full output is in
[`../results/m3_sweep_results.csv`](../results/m3_sweep_results.csv).

## Interpretation

The serial dependent remainder was a major M2 scaling limit. Splitting its
region work by byte range lets it use the idle CPU cores after independent-group
recovery finishes. Operation counts remain logical full-region counts rather
than counting each byte-range chunk as a separate operation. The experiment
shows a runtime improvement for these tested 32 MiB layouts; it does not imply
the same gain for every stripe size or hardware platform.

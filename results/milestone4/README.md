# Milestone 4: final measurements and profiling

The final paper is `docs/Milestone3_IEEE/main.pdf`. The three native datasets
contain 900 validated trials. `final_32mib.csv` covers all nine `(m,s)` cases;
`size_8mib.csv` and `size_128mib.csv` cover `m=2` and all three fields.
`benchmark_summary.csv` records medians, quartiles and ranges;
`comparison_summary.csv` selects the lowest median among 1, 2, 4 and 8 CPU
threads for each workload. `environment.json` records host details, seed,
affinity, run order and source hashes. Each variant uses the same encoded
stripe within a configuration, warms once, then executes in a seeded shuffled
order within each of ten rounds. The three size suites ran in the fixed order
32, 8 and 128 MiB in separate processes. Every trial checks recovered bytes,
checksum and syndrome. The CPU's best tested thread count is selected from
these trials, not a separate tuning dataset.

## Reproduction

Requirements: Linux, GCC/G++ and OpenMP, an NVIDIA CUDA-capable GPU, NVIDIA
driver and CUDA toolkit (including `nvcc`), Python 3 with NumPy and Matplotlib,
Valgrind with Callgrind headers, and NVIDIA Nsight Systems (`nsys`). The paper
requires a LaTeX installation with IEEEtran, Latin Modern and TeX Gyre Termes.
The data file `data/FAST-Coefficients.txt` is required. The tested versions
and processor details are retained in `environment.json`; Nsight Systems
2025.3.2 and Callgrind 3.18.1 produced the included traces. `perf` hardware
counters are not needed and were inaccessible on the measured host.

The retained results are evidence, not a disposable output directory. To
avoid overwriting or mixing old profiles with newly calibrated benchmarks,
first move the entire `results/milestone4` directory to a safe location outside
the repository. Then, from the repository root, run:

```sh
make test
make gpu-test
make m4-benchmark
make m4-profile
make m4-analyze
```

The benchmark and profile scripts refuse to overwrite retained datasets or
reuse existing captures. Profiling follows benchmarking because the CPU
Callgrind-only linker wrapper needs the corresponding native cost weight; the
profile script sets `M4_CALIBRATION_C` automatically from the T=1 native CSV
and records it in each `profiles/cpu_*_calibration.json`. The wrapper is not
part of the native decoder. Changing profiler, hardware or calibration
may change scheduling and results. An interrupted run should be moved aside
and restarted rather than merged with another session. `make m4-analyze`
does not rerun hardware measurements, but it rewrites derived summaries,
plots, table rows and per-API CSVs; copy the directory first if those files
must remain byte-for-byte unchanged.

`profiles/` contains recovery-only Callgrind dumps and inclusive/self reports,
Nsight `.nsys-rep` and SQLite traces, per-API reports, profiled validation CSVs
and command records. `calibration_unfrozen/` retains exploratory CPU captures
from before calibration was fixed; these are **not used** for the paper or
summaries. Profiling is separate from native timing. CUDA kernel intervals can
overlap across streams, and host synchronization calls include waiting for GPU
work; their durations cannot be summed to derive end-to-end latency. CPU
instruction shares under emulation are not native elapsed-time shares.

The plots, generated TeX table rows and `analysis.json` are derived from the
raw CSV and profile databases by `scripts/analyze_milestone4.py`. The paper's
table rows are embedded in the LaTeX source; compare the generated `.tex`
rows when regenerating results.

# Independent adversarial review

An independent read-only audit checked the Milestone 4 requirements, paper,
raw trial CSVs, profiler captures and benchmark sources. It independently
recomputed all 54 final 32 MiB medians, the selected CPU thread counts, the
CPU instruction shares and the six GPU trace summaries. It confirmed the
900 retained trials report PASS and the headline ratios match the evidence.

The review identified and prompted these corrections:

- Corrected the earlier `(m,s)=(3,3)` L3 layer speedup from 1.95× to 1.94×
  (`74.3734125 / 38.239023 = 1.944961...`).
- Explained that the stripe-size experiments ran in fixed 32/8/128 MiB order
  in separate processes and that calibration and system state confound size.
- Added example interquartile ranges and emphasized that best thread counts
  are chosen from the same noisy ten-trial dataset.
- Prevented benchmark overwrites and stale-profile reuse: scripts now refuse
  to proceed until the old complete results directory is moved aside.
- Added reproduction prerequisites, calibration details, profiling scope,
  trace interpretation, and the source of each paper result. Adjusted float
  placement and balanced references in the final PDF.

The audit did not constitute formal peer review or a new hardware experiment.
Remaining limits include one machine and session, ten trials per case, a
small set of sizes and deterministic failure layouts, no confidence intervals,
no `perf` hardware counters, and single-recovery profiles. GPU activity and
host API intervals overlap, so they are not an additive time decomposition.

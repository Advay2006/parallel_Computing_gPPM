# GPU Follow-up Plan

Build a separate GPU backend from a copy of the completed M3 source. Keep M1,
M2, and M3 as fixed CPU checkpoints for comparison.

**Current status (1 November 2026): complete and measured.** CUDA 13.0.88 builds
both variants on the RTX 4050. Focused GPU tests, the 27-trial smoke benchmark,
the 270-trial performance benchmark, Compute Sanitizer memcheck, and CPU tests
pass. See [verified results](../results/milestone_gpu/GPU_Results.md).

## Source and build isolation

- [x] Copy `src/milestone3/` to a separate `src/milestone_gpu/` tree.
- [x] Add CUDA `.cu` kernels and a small C-callable GPU backend API there; leave
  CPU source trees unchanged. **CUDA build/runtime verification passes.**
- [x] Set up the CUDA toolkit/compiler. RTX 4050 (6 GB), driver 580.178.04,
  CUDA 13.0.88. Make discovers `nvcc` on PATH or under `/usr/local/cuda*/bin/`.

## Implementation sequence

1. [x] **GPU scalar baseline:** implement the region multiply-XOR kernel with
   one CUDA thread per field symbol, initially prioritizing correctness over
   speed. Preserve the GF(2^8), GF(2^16), and GF(2^32) polynomials and symbol
   representation.
2. [x] **Whole recovery path:** retain M3's host-side failure analysis, matrix
   preparation, and gPPM strategy choice. Transfer the stripe and prepared
   matrices once; batch region work into kernels rather than launching/copying
   for every `mult_XORs()` call. Recover independent groups, synchronize, then
   recover the dependent remainder; copy the result back once.
3. [x] **GPU optimization:** optimize memory coalescing, field multiplication,
   table access, and kernel fusion. This is a separate GPU optimization layer,
   not a rewrite of the CPU implementation.

“GPU scalar” means the straightforward one-thread-per-symbol CUDA kernel before
GPU-specific optimization. CUDA still schedules threads in SIMD/SIMT warps; the
term distinguishes the reference GPU kernel from the optimized GPU kernel.

## Comparison and completion criteria

- [x] Compare **CPU optimized M3**, **GPU scalar**, and **GPU optimized** on the
  same nine `n=r=16`, `z=1`, 32 MiB SD configurations and field widths.
- [x] Report end-to-end decode time including host/device transfers and kernel
  launches; report the sum of per-kernel CUDA-event durations separately (this
  is aggregate kernel work, not GPU wall time).
- [x] Use byte equality, checksum, and syndrome validation against the CPU result
  for every benchmark variant.
- [x] Measure or calibrate a GPU-specific cost coefficient before using
  `C = u + c·v` to select GPU strategies; CPU-calibrated `c` is not assumed to
  predict GPU costs.
- [x] Save raw CSV and plots under `results/milestone_gpu/` and summarize the
  results separately from the CPU M3 results.

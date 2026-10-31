# Milestone 3 Checklist

- [x] **gPPM selection:** Compare whole-matrix normal, whole-matrix matrix-first, and partitioned PPM using `C = u + c·v`.
- [x] **SIMD:** Add SSSE3/AVX2 region paths for GF(2^8), GF(2^16), and GF(2^32), with scalar fallback; verify results.
- [x] **Parallel remainder:** Split remainder work by byte range; T=8 improved all tested settings, while T=4 had one small regression. Details: [`Milestone_3_Results.md`](Milestone_3_Results.md).
- [ ] **Optional GPU extension:** Explore only if suitable hardware/time is available; it is not required for the CPU M3 work.
- [ ] **Final deliverable — do last:** Prepare the report in **IEEE double-column LaTeX format** using the [official IEEE conference template](https://www.ieee.org/conferences/publishing/templates). Include related work and the M2 analysis (OpenMP/GPU rationale, thread scaling, plateau, and M1 memory) alongside M3 methods and results.

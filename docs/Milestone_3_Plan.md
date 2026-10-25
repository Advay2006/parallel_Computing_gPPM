# Milestone 3 Checklist

- [ ] **Related work:** Review prior PPM/gPPM, SIMD, multicore, and GPU erasure-code implementations; compare their approaches and results with ours.
- [ ] **OpenMP and GPU:** Explain why OpenMP fits our CPU implementation, why a GPU was not selected, and the advantages and disadvantages of both approaches.
- [ ] **Threads and scaling:** Test useful thread counts; explain the performance plateau using the serial remainder, available independent work, and measured hardware effects.
- [ ] **Memory:** Summarize Milestone 1 decoder memory use and how it changes with workload and implementation.
- [ ] **Dynamic decoding:** Implement and benchmark normal-versus-matrix-first selection using the `C = u + c·v` cost model.
- [ ] **SIMD:** Vectorize `mult_XORs`; keep a scalar path and verify identical recovery.
- [ ] **Parallel remainder:** Evaluate splitting remainder region operations across threads and measure whether it improves scaling.
- [ ] **Evaluation:** Run correctness checks and comparable benchmarks; record methods, results, memory use, and limitations. Explore GPU work if hardware and time allow.
- [ ] **Final deliverable — do last:** Write and build the project report in **IEEE double-column LaTeX format**, following the [official IEEE conference templates](https://www.ieee.org/conferences/publishing/templates). Include the related work, design choices, implementation, experiments, results, and limitations.

# Milestone 3 IEEE paper

`main.tex` is the IEEE conference-format CPU/GPU paper source. It uses the
official `IEEEtran` conference class, with the CPU-layer plot included from
`../../results/milestone3/`. The CPU/GPU table and findings use the verified
ten-trial data in `../../results/milestone_gpu/gpu_benchmark.csv`.

Build from this directory on a LaTeX installation that includes `IEEEtran`:

```sh
pdflatex main.tex
pdflatex main.tex
```

The second pass resolves citations and cross-references. Replace the author and
affiliation placeholders in `main.tex` before submission. The generated PDF is
`main.pdf`.

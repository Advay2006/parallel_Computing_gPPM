# Milestone 3 IEEE paper

`main.tex` is the IEEE conference-format CPU/GPU paper source. It uses the
official `IEEEtran` conference class, with the CPU-layer plot included from
`../../results/milestone3/`. The CPU/GPU table and findings use the verified
ten-trial data in `../../results/milestone_gpu/gpu_benchmark.csv`.

Build from this directory on a LaTeX installation that includes `IEEEtran`,
Latin Modern, and TeX Gyre (the Times-compatible Termes text font):

```sh
pdflatex main.tex
pdflatex main.tex
```

The second pass resolves citations and cross-references. The authors and BITS
Pilani, K K Birla Goa Campus affiliation are populated in `main.tex`. The
generated PDF is `main.pdf`.

An independent adversarial scientific audit and follow-up review are summarized
in [`REVIEW.md`](REVIEW.md), including the remaining experimental limitations.

# Milestone 3 IEEE paper

`main.tex` is the final IEEE conference-format CPU/GPU paper source. It uses
the official `IEEEtran` conference class. The initial CPU-layer comparison
uses `../../results/m3_benchmark_*.csv`; the final thread/size scaling,
CPU/GPU comparison and profiling use `../../results/milestone4/`. See its
README for measurement scope, limitations, dependencies and reproduction.

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

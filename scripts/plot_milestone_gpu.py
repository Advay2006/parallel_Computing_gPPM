#!/usr/bin/env python3
"""Plot end-to-end GPU and optimized CPU recovery benchmark results."""

import argparse
import csv
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt


ROOT = Path(__file__).resolve().parents[1]
COLORS = {"gpu_scalar": "#D55E00", "gpu_optimized": "#009E73"}


def summarize(path):
    grouped = defaultdict(list)
    with path.open(newline="", encoding="utf-8") as source:
        for row in csv.DictReader(source):
            if row["status"] != "PASS":
                continue
            key = (int(row["m"]), int(row["s"]), row["implementation"])
            grouped[key].append(float(row["seconds"]))
    return {key: statistics.median(values) for key, values in grouped.items()}


def save(fig, output_dir, stem):
    output_dir.mkdir(parents=True, exist_ok=True)
    png = output_dir / f"{stem}.png"
    svg = output_dir / f"{stem}.svg"
    fig.savefig(png, dpi=240, bbox_inches="tight")
    fig.savefig(svg, bbox_inches="tight")
    plt.close(fig)
    return png, svg


def plot_cpu_speedup(summary, output_dir, source):
    configs = [(m, s) for m in range(1, 4) for s in range(1, 4)]
    labels = [f"{m}/{s}" for m, s in configs]
    x = list(range(len(configs)))
    width = 0.36
    fig, ax = plt.subplots(figsize=(11.5, 5.0))

    for offset, implementation in ((-width / 2, "gpu_scalar"),
                                   (width / 2, "gpu_optimized")):
        speedups = [
            summary[(m, s, "cpu_optimized_m3")]
            / summary[(m, s, implementation)]
            for m, s in configs
        ]
        ax.bar([position + offset for position in x], speedups, width,
               color=COLORS[implementation], label=implementation.replace("_", " "))
    ax.axhline(1.0, color="#30343B", linestyle="--", linewidth=1)
    ax.set_xticks(x, labels, rotation=30, ha="right")
    ax.set_xlabel("Failure configuration (m / s)")
    ax.set_ylabel("End-to-end speedup over optimized CPU M3")
    ax.set_title("GPU Recovery vs CPU M3 (Transfers Included)", fontweight="bold")
    ax.legend()
    fig.text(0.995, 0.005, f"Source: {source.name}", ha="right", va="bottom",
             fontsize=8, color="#667085")
    fig.tight_layout(rect=(0, 0.05, 1, 1))
    return save(fig, output_dir, "gpu_vs_cpu_speedup")


def plot_gpu_optimization(summary, output_dir, source):
    configs = [(m, s) for m in range(1, 4) for s in range(1, 4)]
    labels = [f"{m}/{s}" for m, s in configs]
    gains = [
        summary[(m, s, "gpu_scalar")] / summary[(m, s, "gpu_optimized")]
        for m, s in configs
    ]
    fig, ax = plt.subplots(figsize=(10.5, 4.8))
    bars = ax.bar(range(len(configs)), gains, color="#0072B2", width=0.65)
    for bar, gain in zip(bars, gains):
        ax.text(bar.get_x() + bar.get_width() / 2, gain, f"{gain:.2f}×",
                ha="center", va="bottom", fontsize=8)
    ax.axhline(1.0, color="#30343B", linestyle="--", linewidth=1)
    ax.set_xticks(range(len(configs)), labels, rotation=30, ha="right")
    ax.set_xlabel("Failure configuration (m / s)")
    ax.set_ylabel("GPU scalar time / GPU optimized time")
    ax.set_title("Table-Based GPU Recovery Gain (End-to-End)", fontweight="bold")
    fig.text(0.995, 0.005, f"Source: {source.name}", ha="right", va="bottom",
             fontsize=8, color="#667085")
    fig.tight_layout(rect=(0, 0.05, 1, 1))
    return save(fig, output_dir, "gpu_kernel_optimization_gain")


def plot_latency(summary, output_dir):
    configs = [(m, s) for m in range(1, 4) for s in range(1, 4)]
    fig, ax = plt.subplots(figsize=(11.5, 5))
    for offset, implementation, color in (
        (-0.26, "cpu_optimized_m3", "#0072B2"),
        (0, "gpu_scalar", COLORS["gpu_scalar"]),
        (0.26, "gpu_optimized", COLORS["gpu_optimized"]),
    ):
        ax.bar([i + offset for i in range(len(configs))],
               [1000 * summary[m, s, implementation] for m, s in configs],
               0.26, color=color, label=implementation.replace("_", " "))
    ax.set_xticks(range(len(configs)), [f"{m}/{s}" for m, s in configs])
    ax.set_xlabel("Failure configuration (m / s)")
    ax.set_ylabel("Median recovery time (ms; lower is better)")
    ax.set_title("CPU and GPU Recovery Latency (Transfers Included)", fontweight="bold")
    ax.legend()
    fig.tight_layout()
    return save(fig, output_dir, "gpu_recovery_latency")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path,
                        default=ROOT / "results/milestone_gpu/gpu_benchmark.csv")
    parser.add_argument("--output-dir", type=Path,
                        default=ROOT / "results/milestone_gpu")
    args = parser.parse_args()
    if not args.results.is_file():
        parser.error(f"missing input file: {args.results}")

    plt.rcParams.update({"axes.grid": True, "grid.alpha": 0.6,
                         "legend.frameon": False, "figure.facecolor": "white"})
    summary = summarize(args.results)
    outputs = list(plot_cpu_speedup(summary, args.output_dir, args.results))
    outputs.extend(plot_gpu_optimization(summary, args.output_dir, args.results))
    outputs.extend(plot_latency(summary, args.output_dir))
    print("Generated GPU plots:")
    for path in outputs:
        print(f"  {path}")


if __name__ == "__main__":
    main()

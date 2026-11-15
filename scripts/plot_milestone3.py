#!/usr/bin/env python3
"""Plot cumulative and per-layer M3 speedups against the scalar normal baseline."""

import argparse
import csv
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt


PROJECT_ROOT = Path(__file__).resolve().parents[1]
LAYER_COLORS = {
    "Cost-selected PPM (scalar)": "#667085",
    "Add SIMD": "#0072B2",
    "Add parallel remainder": "#009E73",
}


def read_csv(path):
    with path.open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


def summarize(rows):
    times = defaultdict(list)
    for row in rows:
        if row["status"] != "PASS":
            continue
        key = (int(row["m"]), int(row["s"]), row["implementation"],
               int(row["requested_threads"]))
        times[key].append(float(row["seconds"]))
    return {key: statistics.median(values) for key, values in times.items()}


def save_figure(fig, output_dir, stem):
    output_dir.mkdir(parents=True, exist_ok=True)
    paths = [output_dir / f"{stem}.{ext}" for ext in ("png", "svg")]
    fig.savefig(paths[0], dpi=240, bbox_inches="tight")
    fig.savefig(paths[1], bbox_inches="tight")
    plt.close(fig)
    return paths


def layer_values(scalar, simd, simd_parallel, m, s, threads):
    baseline = scalar[(m, s, "baseline", 1)]
    scalar_ppm = scalar[(m, s, "gppm_serial_remainder", threads)]
    simd_ppm = simd[(m, s, "gppm_serial_remainder", threads)]
    simd_parallel_ppm = simd_parallel[
        (m, s, "gppm_parallel_remainder", threads)
    ]
    cumulative = [
        baseline / scalar_ppm,
        baseline / simd_ppm,
        baseline / simd_parallel_ppm,
    ]
    incremental = [
        baseline / scalar_ppm,
        scalar_ppm / simd_ppm,
        simd_ppm / simd_parallel_ppm,
    ]
    return cumulative, incremental


def plot_cumulative(scalar, simd, simd_parallel, output_dir, source_names):
    configs = [(m, s) for m in range(1, 4) for s in range(1, 4)]
    labels = [f"{m}/{s}" for m, s in configs]
    fig, axes = plt.subplots(1, 2, figsize=(14, 5), sharey=True)
    layer_names = list(LAYER_COLORS)

    for ax, threads in zip(axes, (4, 8)):
        values_by_layer = [[] for _ in layer_names]
        for m, s in configs:
            cumulative, _ = layer_values(scalar, simd, simd_parallel, m, s, threads)
            for i, value in enumerate(cumulative):
                values_by_layer[i].append(value)
        for name, values in zip(layer_names, values_by_layer):
            ax.plot(labels, values, marker="o", linewidth=2.5, markersize=6,
                    color=LAYER_COLORS[name], label=name)
        ax.axhline(1.0, color="#30343B", linewidth=1, linestyle="--")
        ax.set_title(f"T={threads}", fontweight="bold")
        ax.set_xlabel("Failure configuration (m / s)")
        ax.tick_params(axis="x", rotation=35)
        ax.legend(fontsize=12)
        ax.tick_params(axis="both", labelsize=12)

    axes[0].set_ylabel("Cumulative speedup vs scalar normal baseline")
    fig.suptitle("Cumulative M3 Optimization Layers", fontsize=20, fontweight="bold")
    fig.text(0.995, 0.005, f"Sources: {source_names}", ha="right", va="bottom",
             fontsize=10, color="#667085")
    fig.tight_layout(rect=(0, 0.05, 1, 0.92))
    return save_figure(fig, output_dir, "m3_cumulative_speedup")


def plot_incremental(scalar, simd, simd_parallel, output_dir, source_names):
    configs = [(m, s) for m in range(1, 4) for s in range(1, 4)]
    labels = [f"{m}/{s}" for m, s in configs]
    positions = list(range(len(configs)))
    layer_names = list(LAYER_COLORS)
    width = 0.24
    fig, axes = plt.subplots(1, 2, figsize=(14, 5), sharey=True)

    for ax, threads in zip(axes, (4, 8)):
        for i, name in enumerate(layer_names):
            values = [
                layer_values(scalar, simd, simd_parallel, m, s, threads)[1][i]
                for m, s in configs
            ]
            offset = (i - 1) * width
            ax.bar([x + offset for x in positions], values, width,
                   color=LAYER_COLORS[name], label=name)
        ax.axhline(1.0, color="#30343B", linewidth=1, linestyle="--")
        ax.set_title(f"T={threads}", fontweight="bold")
        ax.set_xticks(positions, labels, rotation=35, ha="right")
        ax.set_xlabel("Failure configuration (m / s)")
        ax.legend(fontsize=12)
        ax.tick_params(axis="both", labelsize=12)

    axes[0].set_ylabel("Incremental speedup over previous layer")
    fig.suptitle("Benefit Added at Each M3 Layer", fontsize=20, fontweight="bold")
    fig.text(0.995, 0.005, f"Sources: {source_names}", ha="right", va="bottom",
             fontsize=10, color="#667085")
    fig.tight_layout(rect=(0, 0.05, 1, 0.92))
    return save_figure(fig, output_dir, "m3_incremental_speedup")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-dir", type=Path, default=PROJECT_ROOT / "results")
    parser.add_argument("--output-dir", type=Path,
                        default=PROJECT_ROOT / "results" / "milestone3")
    args = parser.parse_args()

    paths = {
        "scalar": args.results_dir / "m3_benchmark_scalar.csv",
        "simd_layers": args.results_dir / "m3_benchmark_simd_layers.csv",
    }
    for path in paths.values():
        if not path.is_file():
            parser.error(f"missing input file: {path}")

    plt.rcParams.update(
        {
            "font.family": "DejaVu Sans",
            "font.size": 12,
            "axes.titlesize": 16,
            "axes.labelsize": 16,
            "axes.edgecolor": "#30343B",
            "axes.linewidth": 0.8,
            "axes.grid": True,
            "grid.color": "#D9DDE3",
            "grid.linewidth": 0.7,
            "grid.alpha": 0.8,
            "legend.frameon": False,
            "figure.facecolor": "white",
            "axes.facecolor": "#FAFBFC",
            "savefig.facecolor": "white",
        }
    )
    scalar = summarize(read_csv(paths["scalar"]))
    simd = summarize(read_csv(paths["simd_layers"]))
    simd_parallel = simd
    source_names = ", ".join(path.name for path in paths.values())
    outputs = []
    outputs.extend(plot_cumulative(scalar, simd, simd_parallel,
                                   args.output_dir, source_names))
    outputs.extend(plot_incremental(scalar, simd, simd_parallel,
                                    args.output_dir, source_names))
    print("Generated M3 layer plots:")
    for output in outputs:
        print(f"  {output}")


if __name__ == "__main__":
    main()

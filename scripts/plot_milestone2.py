#!/usr/bin/env python3
"""Generate presentation-ready Milestone 2 plots from benchmark CSV files."""

import argparse
import csv
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


COLORS = {1: "#0072B2", 2: "#D55E00", 3: "#009E73"}
MARKERS = {1: "o", 2: "s", 3: "^"}
THREADS = [1, 2, 4, 8]


def read_csv(path):
    with path.open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


def configure_style():
    plt.rcParams.update(
        {
            "font.family": "DejaVu Sans",
            "font.size": 11,
            "axes.titlesize": 13,
            "axes.labelsize": 11,
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


def save_figure(fig, output_dir, stem):
    output_dir.mkdir(parents=True, exist_ok=True)
    png = output_dir / f"{stem}.png"
    svg = output_dir / f"{stem}.svg"
    fig.savefig(png, dpi=240, bbox_inches="tight")
    fig.savefig(svg, bbox_inches="tight")
    plt.close(fig)
    return png, svg


def benchmark_summary(rows):
    seconds = defaultdict(list)
    counts = {}
    fields = {}

    for row in rows:
        if row["status"] != "PASS":
            continue
        m, s = int(row["m"]), int(row["s"])
        implementation = row["implementation"]
        threads = int(row["requested_threads"])
        key = (m, s, implementation, threads)
        seconds[key].append(float(row["seconds"]))
        counts[(m, s, implementation, threads)] = int(row["measured_mult_xors"])
        fields[(m, s)] = int(row["w"])

    summary = {}
    for m in range(1, 4):
        for s in range(1, 4):
            baseline_key = (m, s, "baseline", 1)
            if baseline_key not in seconds:
                raise ValueError(f"missing baseline results for m={m}, s={s}")
            baseline = statistics.median(seconds[baseline_key])
            config = {
                "baseline_seconds": baseline,
                "baseline_count": counts[baseline_key],
                "w": fields[(m, s)],
                "ppm": {},
            }
            for threads in THREADS:
                ppm_key = (m, s, "ppm", threads)
                if ppm_key not in seconds:
                    raise ValueError(
                        f"missing PPM results for m={m}, s={s}, T={threads}"
                    )
                elapsed = statistics.median(seconds[ppm_key])
                config["ppm"][threads] = {
                    "seconds": elapsed,
                    "speedup": baseline / elapsed,
                    "count": counts[ppm_key],
                }
            summary[(m, s)] = config
    return summary


def add_source(fig):
    fig.text(
        0.995,
        0.005,
        "Source: results/ppm_benchmark_results.csv",
        ha="right",
        va="bottom",
        fontsize=8,
        color="#667085",
    )


def plot_speedup(summary, output_dir):
    fig, axes = plt.subplots(1, 3, figsize=(13.5, 4.4), sharey=True)
    for m, ax in enumerate(axes, start=1):
        for s in range(1, 4):
            values = [summary[(m, s)]["ppm"][t]["speedup"] for t in THREADS]
            ax.plot(
                THREADS,
                values,
                color=COLORS[s],
                marker=MARKERS[s],
                linewidth=2.2,
                markersize=6,
                label=f"s={s}, GF(2^{summary[(m, s)]['w']})",
            )
        ax.axhline(1.0, color="#30343B", linewidth=1, linestyle="--")
        ax.set_title(f"m = {m}", fontweight="bold")
        ax.set_xlabel("OpenMP threads")
        ax.set_xticks(THREADS)
        ax.legend(fontsize=9, loc="upper left")
    axes[0].set_ylabel("Speedup over sequential baseline")
    axes[0].set_ylim(bottom=0.9)
    fig.suptitle("PPM Decode Scaling", fontsize=18, fontweight="bold", y=0.98)
    fig.text(
        0.5,
        0.90,
        "32 MiB codeword, n=16, r=16, z=1; median of 10 trials",
        ha="center",
        color="#475467",
    )
    add_source(fig)
    fig.tight_layout(rect=(0, 0.04, 1, 0.85))
    return save_figure(fig, output_dir, "ppm_speedup_scaling")


def plot_throughput(summary, output_dir):
    fig, axes = plt.subplots(1, 3, figsize=(13.5, 4.4), sharey=True)
    x = np.arange(5)
    labels = ["Baseline", "T=1", "T=2", "T=4", "T=8"]
    for m, ax in enumerate(axes, start=1):
        for s in range(1, 4):
            config = summary[(m, s)]
            data_mib = 32.0 * (256 - (m * 16 + s)) / 256.0
            values = [data_mib / config["baseline_seconds"]]
            values.extend(data_mib / config["ppm"][t]["seconds"] for t in THREADS)
            ax.plot(
                x,
                values,
                color=COLORS[s],
                marker=MARKERS[s],
                linewidth=2.2,
                markersize=6,
                label=f"s={s}, GF(2^{config['w']})",
            )
        ax.set_title(f"m = {m}", fontweight="bold")
        ax.set_xticks(x, labels, rotation=30, ha="right")
        ax.set_xlabel("Decoder")
        ax.legend(fontsize=9, loc="upper right")
    axes[0].set_ylabel("Useful-data throughput (MiB/s)")
    axes[0].set_ylim(bottom=0)
    fig.suptitle("Baseline vs PPM Decode Throughput", fontsize=18, fontweight="bold", y=0.98)
    fig.text(
        0.5,
        0.90,
        "32 MiB codeword, n=16, r=16, z=1; median of 10 trials",
        ha="center",
        color="#475467",
    )
    add_source(fig)
    fig.tight_layout(rect=(0, 0.04, 1, 0.85))
    return save_figure(fig, output_dir, "ppm_throughput_scaling")


def plot_benchmark_work_reduction(summary, output_dir):
    reduction = np.zeros((3, 3))
    for m in range(1, 4):
        for s in range(1, 4):
            config = summary[(m, s)]
            ppm_count = config["ppm"][1]["count"]
            reduction[m - 1, s - 1] = 100.0 * (
                1.0 - ppm_count / config["baseline_count"]
            )

    fig, ax = plt.subplots(figsize=(7.4, 5.5))
    image = ax.imshow(reduction, cmap="YlGnBu", vmin=0, vmax=max(30, reduction.max()))
    for row in range(3):
        for col in range(3):
            color = "white" if reduction[row, col] > reduction.max() * 0.58 else "#101828"
            ax.text(
                col,
                row,
                f"{reduction[row, col]:.1f}%",
                ha="center",
                va="center",
                fontsize=15,
                fontweight="bold",
                color=color,
            )
    ax.set_xticks(range(3), ["s=1", "s=2", "s=3"])
    ax.set_yticks(range(3), ["m=1", "m=2", "m=3"])
    ax.set_xlabel("Additional sector failures (s)")
    ax.set_ylabel("Failed disks (m)")
    ax.grid(False)
    with plt.rc_context({"axes.grid": False}):
        colorbar = fig.colorbar(image, ax=ax, fraction=0.046, pad=0.04)
    colorbar.set_label("Fewer mult_XORs operations (%)")
    ax.set_title("PPM Algorithmic Work Reduction", fontsize=18, fontweight="bold", pad=24)
    ax.text(
        0.5,
        1.02,
        "T=1 operation counts; n=16, r=16, z=1",
        transform=ax.transAxes,
        ha="center",
        color="#475467",
    )
    add_source(fig)
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    return save_figure(fig, output_dir, "ppm_work_reduction")


def plot_sweep_distribution(baseline_rows, ppm_rows, output_dir):
    baseline = {}
    for row in baseline_rows:
        if row["roundtrip"] == "PASS":
            key = tuple(int(row[name]) for name in ("n", "m", "s", "r", "w", "z"))
            baseline[key] = int(row["measured"])

    grouped = defaultdict(list)
    for row in ppm_rows:
        if row["status"] != "PASS":
            continue
        key = tuple(int(row[name]) for name in ("n", "m", "s", "r", "w", "z"))
        if key not in baseline:
            continue
        base_count = baseline[key]
        ppm_count = int(row["C_t1"])
        grouped[(int(row["m"]), int(row["s"]))].append(
            100.0 * (1.0 - ppm_count / base_count)
        )

    keys = [(m, s) for m in range(1, 4) for s in range(1, 4)]
    values = [grouped[key] for key in keys]
    if any(not group for group in values):
        raise ValueError("baseline and PPM sweep files do not contain matching data")

    fig, ax = plt.subplots(figsize=(11.5, 5.8))
    boxes = ax.boxplot(
        values,
        labels=[f"m={m}\ns={s}" for m, s in keys],
        whis=(5, 95),
        showfliers=False,
        patch_artist=True,
        medianprops={"color": "#101828", "linewidth": 1.8},
        whiskerprops={"color": "#667085"},
        capprops={"color": "#667085"},
    )
    for box, (_, s) in zip(boxes["boxes"], keys):
        box.set_facecolor(COLORS[s])
        box.set_alpha(0.78)
        box.set_edgecolor("#344054")
    for position, group in enumerate(values, start=1):
        median = statistics.median(group)
        ax.text(position, median, f" {median:.1f}%", va="center", fontsize=8)
    ax.axhline(0, color="#30343B", linewidth=1)
    ax.set_ylabel("Fewer mult_XORs operations (%)")
    ax.set_xlabel("SD failure tolerance configuration")
    ax.set_title(
        "PPM Work Reduction Across 7,833 Failure Layouts",
        fontsize=18,
        fontweight="bold",
        pad=20,
    )
    ax.text(
        0.5,
        1.01,
        "Boxes show the 25th-75th percentiles; whiskers show the 5th-95th percentiles",
        transform=ax.transAxes,
        ha="center",
        color="#475467",
    )
    fig.text(
        0.995,
        0.005,
        "Source: results/sweep_results.csv and results/ppm_sweep_results.csv",
        ha="right",
        va="bottom",
        fontsize=8,
        color="#667085",
    )
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    return save_figure(fig, output_dir, "ppm_sweep_work_reduction")


def plot_figure3(output_dir):
    values = [35, 29]
    fig, ax = plt.subplots(figsize=(7.2, 5.2))
    bars = ax.bar(
        ["Sequential baseline", "PPM"],
        values,
        color=["#667085", "#0072B2"],
        width=0.58,
    )
    ax.bar_label(bars, labels=["35 operations", "29 operations"], padding=7,
                 fontsize=13, fontweight="bold")
    ax.annotate(
        "17.1% fewer region operations",
        xy=(1, 29),
        xytext=(0.52, 39),
        arrowprops={"arrowstyle": "->", "color": "#D55E00", "linewidth": 1.8},
        color="#D55E00",
        fontsize=12,
        fontweight="bold",
        ha="center",
    )
    ax.set_ylim(0, 43)
    ax.set_ylabel("mult_XORs operations (C)")
    ax.set_title("Paper Figure 3 Reproduced", fontsize=18, fontweight="bold", pad=18)
    ax.text(
        0.5,
        1.01,
        "SD(4,4,m=1,s=1,w=8), failures {2,6,10,13,14}",
        transform=ax.transAxes,
        ha="center",
        color="#475467",
    )
    ax.grid(axis="x", visible=False)
    fig.tight_layout()
    return save_figure(fig, output_dir, "ppm_figure3_cost")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--results-dir", type=Path, default=Path("results"), help="CSV directory"
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("results/plots"),
        help="plot output directory",
    )
    args = parser.parse_args()

    benchmark_path = args.results_dir / "ppm_benchmark_results.csv"
    baseline_sweep_path = args.results_dir / "sweep_results.csv"
    ppm_sweep_path = args.results_dir / "ppm_sweep_results.csv"
    for path in (benchmark_path, baseline_sweep_path, ppm_sweep_path):
        if not path.is_file():
            parser.error(f"missing input file: {path}")

    configure_style()
    benchmark_rows = read_csv(benchmark_path)
    summary = benchmark_summary(benchmark_rows)
    outputs = []
    outputs.extend(plot_speedup(summary, args.output_dir))
    outputs.extend(plot_throughput(summary, args.output_dir))
    outputs.extend(plot_benchmark_work_reduction(summary, args.output_dir))
    outputs.extend(
        plot_sweep_distribution(
            read_csv(baseline_sweep_path), read_csv(ppm_sweep_path), args.output_dir
        )
    )
    outputs.extend(plot_figure3(args.output_dir))

    print("Generated Milestone 2 plots:")
    for output in outputs:
        print(f"  {output}")


if __name__ == "__main__":
    main()

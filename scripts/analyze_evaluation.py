#!/usr/bin/env python3
"""Analyze results/evaluation/raw/*.csv: thread scaling, taper and GPU-crossover
metrics, ablation tables and reproductions of the gPPM paper's evaluation figures.

Outputs (results/evaluation/):
  summary_*.csv          medians per configuration
  taper_metrics.csv      where parallel speedup saturates, per implementation
  gpu_crossover.csv      where the GPU overtakes / is overtaken by the CPU
  ablation_*.csv         ablation tables
  tables.md              every table, as Markdown
  figures/*.png|svg      plots
"""
import csv
import math
import os
import statistics
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.lines import Line2D  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "results", "evaluation")
RAW = os.path.join(OUT, "raw")
FIG = os.path.join(OUT, "figures")
M4 = os.path.join(ROOT, "results", "milestone4", "benchmark_summary.csv")

# Reference categorical palette (dataviz skill, light mode), fixed order.
BLUE, ORANGE, AQUA, YELLOW, MAGENTA, GREEN, VIOLET, RED = (
    "#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#4a3aa7", "#e34948")
INK, INK2, GRID = "#0b0b0b", "#52514e", "#e4e3df"
S_COLOR = {1: BLUE, 2: ORANGE, 3: AQUA}
M_STYLE = {1: (0, ()), 2: (0, (5, 2)), 3: (0, (1.5, 1.5))}
M_MARK = {1: "o", 2: "s", 3: "^"}
W_OF_S = {1: 8, 2: 16, 3: 32}
MS = [(m, s) for m in (1, 2, 3) for s in (1, 2, 3)]

IMPL = {  # label -> (build, implementation, remainder)
    "M1 sequential (scalar)": ("m2_scalar", "baseline", "serial"),
    "SD baseline (SIMD)": ("m3_simd", "baseline", "serial"),
    "M2 PPM (scalar)": ("m2_scalar", "ppm", "serial"),
    "M2 PPM + SIMD": ("m2_simd", "ppm", "serial"),
    "M3 gPPM scalar": ("m3_scalar", "ppm", "parallel"),
    "M3 gPPM scalar, serial remainder": ("m3_scalar", "ppm", "serial"),
    "M3 gPPM SIMD, serial remainder": ("m3_simd", "ppm", "serial"),
    "M3 gPPM SIMD": ("m3_simd", "ppm", "parallel"),
}
TABLES = []


def style():
    plt.rcParams.update({
        "font.size": 9, "axes.edgecolor": INK2, "axes.labelcolor": INK, "xtick.color": INK2,
        "ytick.color": INK2, "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.6,
        "axes.spines.top": False, "axes.spines.right": False, "lines.linewidth": 2,
        "lines.markersize": 4, "legend.frameon": False, "savefig.facecolor": "white",
        "figure.facecolor": "white", "axes.titlesize": 9.5, "axes.titleweight": "bold"})


def save(fig, stem):
    os.makedirs(FIG, exist_ok=True)
    fig.savefig(os.path.join(FIG, stem + ".png"), dpi=200, bbox_inches="tight")
    fig.savefig(os.path.join(FIG, stem + ".svg"), bbox_inches="tight")
    plt.close(fig)


def read(path):
    if not os.path.exists(path):
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def medians(rows):
    groups = defaultdict(list)
    for r in rows:
        key = (r["build"], r["implementation"], int(r["n"]), int(r["m"]), int(r["s"]), int(r["r"]),
               int(r["w"]), int(r["z"]), int(r["stripe_mib"]), int(r["threads"]),
               r["remainder"] if r["implementation"] == "ppm" else "serial")
        groups[key].append((float(r["seconds"]) * 1e3, float(r["data_MiB_s"]), int(r["used_partitioning"])))
    out = {}
    for key, vals in groups.items():
        ms = sorted(v[0] for v in vals)
        q = statistics.quantiles(ms, n=4) if len(ms) > 1 else [ms[0]] * 3
        out[key] = {"ms": statistics.median(ms), "q1": q[0], "q3": q[2],
                    "mibs": statistics.median(v[1] for v in vals), "part": vals[0][2], "trials": len(ms)}
    return out


def write_csv(name, header, rows):
    with open(os.path.join(OUT, name), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)


def md_table(title, header, rows, note=None):
    lines = [f"### {title}", ""]
    if note:
        lines += [note, ""]
    lines.append("| " + " | ".join(header) + " |")
    lines.append("|" + "|".join("---" for _ in header) + "|")
    lines += ["| " + " | ".join(str(c) for c in row) + " |" for row in rows]
    TABLES.append("\n".join(lines) + "\n")


def f2(x, d=2):
    return "—" if x is None or (isinstance(x, float) and math.isnan(x)) else f"{x:.{d}f}"


def pct(x):
    return "—" if x is None else f"{x * 100:+.1f}%"


# --------------------------------------------------------------------------- scaling
class Scaling:
    def __init__(self, rows, tag=None):
        self.med = medians([r for r in rows if tag is None or r["tag"] == tag])
        self.threads = sorted({k[9] for k in self.med})

    def t(self, label, m, s, T=1):
        build, impl, rem = IMPL[label]
        T = 1 if impl == "baseline" else T
        v = self.med.get((build, impl, 16, m, s, 16, W_OF_S[s], 1, 32, T, rem))
        return v["ms"] if v else None

    def curve(self, label, m, s):
        return [(T, self.t(label, m, s, T)) for T in self.threads if self.t(label, m, s, T)]


def amdahl_fraction(curve, tmax):
    pts = [(T, c1 / c) for (T, c), c1 in zip(curve, [curve[0][1]] * len(curve)) if 1 < T <= tmax]
    num = sum((1 - 1 / T) * (1 / S - 1 / T) for T, S in pts)
    den = sum((1 - 1 / T) ** 2 for T, _ in pts)
    return max(0.0, min(1.0, num / den)) if den else None


def taper(curve):
    """Speedup saturation metrics for one (implementation, m, s) curve."""
    t1 = curve[0][1]
    S = {T: t1 / t for T, t in curve}
    Ts = sorted(S)
    peak_T = max(Ts, key=lambda T: S[T])
    peak = S[peak_T]
    t90 = next(T for T in Ts if S[T] >= 0.9 * peak)
    # Last thread count at which adding threads still bought >=5% more speed;
    # beyond it, every further step gains <5% (parallelization has worn off).
    knee = 1
    for a, b in zip(Ts, Ts[1:]):
        if S[b] >= S[a] * 1.05 and S[b] >= max(S[x] for x in Ts if x <= a) * 1.05:
            knee = b
    eff50 = next((T for T in Ts if T > 1 and S[T] / T < 0.5), None)
    return {"S": S, "peak_T": peak_T, "peak": peak, "t90": t90, "knee": knee, "eff50": eff50}


def scaling_analysis(sc, gpu):
    impls = ["M2 PPM (scalar)", "M2 PPM + SIMD", "M3 gPPM SIMD"]
    ncpu = os.cpu_count() or 12
    rows, md = [], []
    metrics = {}
    for label in impls + ["M3 gPPM scalar"]:
        for m, s in MS:
            c = sc.curve(label, m, s)
            if len(c) < 3:
                continue
            tp = taper(c)
            f = amdahl_fraction(c, 8)
            metrics[(label, m, s)] = tp
            m1 = sc.t("M1 sequential (scalar)", m, s)
            rows.append([label, m, s, W_OF_S[s], f2(c[0][1], 3), tp["peak_T"], f2(tp["peak"]),
                         f2(m1 / dict(c)[tp["peak_T"]]), tp["knee"], tp["t90"], tp["eff50"] or "—",
                         f2(f, 3) if f is not None else "—", f2(1 / f, 1) if f else "—"])
    header = ["implementation", "m", "s", "w", "T1 ms", "peak T", "peak speedup (vs own T=1)",
              "peak speedup vs M1", "knee T (last ≥5% gain)", "T reaching 90% of peak",
              "first T with efficiency <50%", "Amdahl serial fraction f", "Amdahl limit 1/f"]
    write_csv("taper_metrics.csv", header, rows)
    md_table("Where parallel speedup tapers off (32 MiB, n=r=16, z=1)", header,
             [r for r in rows if r[0] in impls],
             f"This machine has {ncpu} logical CPUs (4 P-cores with Hyper-Threading = 8 threads, plus 4 "
             "E-cores). **Knee T** is the last thread count where adding threads still gained ≥5%; past "
             "it the curve is flat or falling. The Amdahl fraction is fitted on T≤8.")

    # Full speedup-by-threads matrix per implementation.
    for label in impls:
        header = ["m", "s", "w"] + [f"T={T}" for T in sc.threads]
        body, csvrows = [], []
        for m, s in MS:
            tp = metrics.get((label, m, s))
            if not tp:
                continue
            vals = [tp["S"].get(T) for T in sc.threads]
            body.append([m, s, W_OF_S[s]] + [("**" + f2(v) + "**") if T == tp["peak_T"] else f2(v)
                                             for T, v in zip(sc.threads, vals)])
            csvrows.append([label, m, s, W_OF_S[s]] + [f2(v, 4) for v in vals])
        md_table(f"Speedup vs own T=1 — {label}", header, body, "Bold = peak.")
        write_csv(f"speedup_{label.split()[0].lower()}{'_simd' if 'SIMD' in label and 'M2' in label else ''}.csv",
                  ["implementation", "m", "s", "w"] + [f"T{T}" for T in sc.threads], csvrows)

    plot_scaling_individual(sc, metrics, impls)
    plot_scaling_overview(sc, metrics, impls)
    return metrics


def plot_scaling_individual(sc, metrics, impls):
    for label in impls:
        fig, ax = plt.subplots(figsize=(6.4, 4.2))
        for m, s in MS:
            tp = metrics.get((label, m, s))
            if not tp:
                continue
            Ts = sorted(tp["S"])
            ax.plot(Ts, [tp["S"][T] for T in Ts], color=S_COLOR[s], linestyle=M_STYLE[m],
                    marker=M_MARK[m], label=f"m={m}, s={s} (w={W_OF_S[s]})")
            ax.plot([tp["knee"]], [tp["S"][tp["knee"]]], marker="o", markersize=9, mfc="none",
                    mec=INK, mew=1.2)
        Ts = sc.threads
        ax.plot(Ts, Ts, color=INK2, linewidth=0.8, linestyle=":", label="ideal (linear)")
        ax.axvline(8, color=INK2, linewidth=0.8)
        ax.axvline(os.cpu_count() or 12, color=INK2, linewidth=0.8)
        ax.text(8.1, 0.62, "8 = P-core threads", rotation=90, va="bottom", color=INK2, fontsize=7,
                transform=ax.get_xaxis_transform())
        ax.text((os.cpu_count() or 12) + 0.1, 0.62, "all logical CPUs", rotation=90, va="bottom",
                color=INK2, fontsize=7, transform=ax.get_xaxis_transform())
        ax.set_ylim(0, max(4, max(max(tp["S"].values()) for (lb, _, _), tp in metrics.items()
                                  if lb == label) * 1.25))
        ax.set_xlabel("OpenMP threads T")
        ax.set_ylabel("speedup vs same implementation at T=1")
        ax.set_title(f"{label}: thread scaling (32 MiB, n=r=16); open circles = knee")
        ax.legend(loc="upper left", bbox_to_anchor=(1.01, 1), fontsize=7)
        save(fig, "scaling_" + label.lower().replace(" ", "_").replace("+", "plus").replace("(", "")
             .replace(")", ""))


def plot_scaling_overview(sc, metrics, impls):
    fig, axes = plt.subplots(3, 3, figsize=(10, 8), sharex=True)
    colors = {impls[0]: BLUE, impls[1]: YELLOW, impls[2]: ORANGE}
    for (m, s), ax in zip(MS, axes.flat):
        for label in impls:
            tp = metrics.get((label, m, s))
            if tp:
                Ts = sorted(tp["S"])
                ax.plot(Ts, [tp["S"][T] for T in Ts], color=colors[label], marker="o", label=label)
                ax.axvline(tp["knee"], color=colors[label], linewidth=0.8, linestyle=":")
        ax.set_title(f"m={m}, s={s} (GF(2^{W_OF_S[s]}))")
        ax.set_ylim(bottom=0)
        ax.set_xticks([1, 2, 4, 6, 8, 10, 12, 14, 16])
    for ax in axes[-1]:
        ax.set_xlabel("threads T")
    for ax in axes[:, 0]:
        ax.set_ylabel("speedup vs own T=1")
    axes[0, 0].legend(fontsize=7)
    fig.suptitle("Thread scaling per configuration; dotted lines mark each knee", fontweight="bold")
    fig.tight_layout()
    save(fig, "scaling_overview_grid")


# --------------------------------------------------------------------------- GPU
def load_gpu():
    rows = read(M4)
    g = defaultdict(dict)
    for r in rows:
        key = (int(r["stripe_mib"]), int(r["m"]), int(r["s"]))
        name = r["implementation"] + (f"_T{r['threads']}" if r["implementation"] == "cpu_l3" else "")
        g[key][name] = float(r["median_ms"])
    return g


def crossover(curve, target):
    """Smallest (interpolated) T at which the CPU time drops to `target` ms."""
    if curve[0][1] <= target:
        return 1.0
    for (t0, c0), (t1, c1) in zip(curve, curve[1:]):
        if c1 <= target:
            return t0 + (c0 - target) / (c0 - c1) * (t1 - t0)
    return None


def plot_combined(sc, metrics, gpu, pinned=None):
    fig, axes = plt.subplots(3, 3, figsize=(10.5, 8.4), sharex=True)
    for (m, s), ax in zip(MS, axes.flat):
        m1 = sc.t("M1 sequential (scalar)", m, s)
        g = gpu.get((32, m, s), {})
        for label, color in (("M2 PPM (scalar)", BLUE), ("M3 gPPM SIMD", ORANGE)):
            c = sc.curve(label, m, s)
            ax.plot([T for T, _ in c], [m1 / t for _, t in c], color=color, marker="o", label=label)
        if pinned:
            c = pinned.curve("M3 gPPM SIMD", m, s)
            ax.plot([T for T, _ in c], [m1 / t for _, t in c], color=ORANGE, linestyle=(0, (4, 2)),
                    linewidth=1.5, label="M3 gPPM SIMD, pinned P-cores first")
        verdict = ""
        if "gpu_table" in g:
            best_gpu = min(g["gpu_table"], g["gpu_scalar"])
            proj = best_gpu * sc.t("M3 gPPM SIMD", m, s, 1) / g["cpu_l3_T1"]
            ax.axhline(m1 / proj, color=AQUA, label="GPU (best kernel, normalized)")
            curves = [sc.curve("M3 gPPM SIMD", m, s)]
            if pinned:
                curves.append(pinned.curve("M3 gPPM SIMD", m, s))
            xs = [crossover(c, proj) for c in curves]
            if all(x == 1.0 for x in xs):
                verdict = "CPU wins at T=1"
            elif all(x is None for x in xs):
                verdict = "GPU wins at every T"
            else:
                T3 = min(x for x in xs if x is not None)
                ax.axvline(T3, color=AQUA, linestyle=":", linewidth=1)
                who = "M3" if xs[0] is not None else "pinned M3"
                verdict = f"{who} passes GPU at T≈{T3:.1f}"
        ax.set_title(f"m={m}, s={s} (GF(2^{W_OF_S[s]})) — {verdict}" if verdict else
                     f"m={m}, s={s} (GF(2^{W_OF_S[s]}))", fontsize=8.5)
        ax.set_ylim(bottom=0)
        ax.set_xticks([1, 2, 4, 6, 8, 10, 12, 14, 16])
    for ax in axes[-1]:
        ax.set_xlabel("CPU threads T")
    for ax in axes[:, 0]:
        ax.set_ylabel("speedup vs M1 sequential")
    handles, labels = axes.flat[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=4, bbox_to_anchor=(0.5, -0.04), fontsize=8)
    fig.suptitle("CPU M2, CPU M3 and GPU recovery, 32 MiB stripe (speedup over the scalar sequential "
                 "decoder)", fontweight="bold")
    fig.text(0.5, -0.07, "GPU times come from the RTX 4050 machine and are scaled by the ratio of M3 T=1 "
             "times between the two machines.", ha="center", fontsize=7.5, color=INK2)
    fig.tight_layout()
    save(fig, "combined_m2_m3_gpu_speedup")


def plot_gpu_projected(sc, gpu, pinned):
    """GPU speedup over CPU M3 across T=1..16 on this CPU, GF(2^32) workloads."""
    fig, ax = plt.subplots(figsize=(6.4, 4.0))
    ax.axvspan(0.5, 4.5, color=GRID, alpha=0.5, linewidth=0)
    for (lo, hi, t) in ((0.5, 4.5, "P-cores"), (4.5, 8.5, "E-cores"), (8.5, 12.5, "HT siblings")):
        ax.text((lo + hi) / 2, 0.97, t, transform=ax.get_xaxis_transform(), ha="center", va="top",
                fontsize=7, color=INK2)
    for m, color in ((1, BLUE), (2, ORANGE), (3, AQUA)):
        g = gpu.get((32, m, 3))
        if not g:
            continue
        proj = min(g["gpu_table"], g["gpu_scalar"]) * sc.t("M3 gPPM SIMD", m, 3, 1) / g["cpu_l3_T1"]
        for src, ls, name in ((sc, (0, (4, 2)), "OS-scheduled"), (pinned, (0, ()), "pinned")):
            if src is None:
                continue
            c = src.curve("M3 gPPM SIMD", m, 3)
            ax.plot([T for T, _ in c], [t / proj for _, t in c], color=color, linestyle=ls,
                    marker="o" if name == "pinned" else None, label=f"m={m}, {name}")
    ax.axhline(1, color=INK, linewidth=1)
    ax.text(16.3, 1.0, "parity", va="center", fontsize=7, color=INK)
    ax.set_xticks([1, 2, 4, 6, 8, 10, 12, 14, 16])
    ax.set_xlim(0.5, 17.5)
    ax.set_xlabel("CPU M3 threads T (i5-13420H)")
    ax.set_ylabel("GPU speedup over CPU (CPU ms / GPU ms)")
    ax.set_title("GF(2^32), 32 MiB: GPU lead vs CPU threads, projected T=1–16")
    ax.legend(loc="upper left", bbox_to_anchor=(1.01, 1), fontsize=7)
    save(fig, "gpu_speedup_vs_cpu_threads_projected")


def gpu_analysis(sc, gpu, pinned=None):
    # (1) Same-machine (Core Ultra 9 185H + RTX 4050), from Milestone 4 data.
    rows, body = [], []
    for (mib, m, s), g in sorted(gpu.items()):
        if "gpu_table" not in g:
            continue
        best_gpu, kernel = min((g["gpu_table"], "table"), (g["gpu_scalar"], "scalar"))
        curve = [(T, g[f"cpu_l3_T{T}"]) for T in (1, 2, 4, 8)]
        Tx = crossover(curve, best_gpu)
        ratios = [c / best_gpu for _, c in curve]
        if Tx is None:
            verdict = "GPU faster at every tested T (1–8)"
        elif Tx == 1.0:
            verdict = "CPU faster even at T=1"
        else:
            verdict = f"GPU faster while CPU T < {Tx:.1f}"
        rows.append([mib, m, s, W_OF_S[s], f2(best_gpu), kernel] + [f2(c) for _, c in curve] +
                    [f2(r) for r in ratios] + [f2(Tx, 1) if Tx else "none", verdict])
    header = ["stripe MiB", "m", "s", "w", "GPU ms", "GPU kernel", "CPU T1 ms", "CPU T2 ms",
              "CPU T4 ms", "CPU T8 ms", "CPU/GPU T1", "CPU/GPU T2", "CPU/GPU T4", "CPU/GPU T8",
              "crossover T", "verdict"]
    write_csv("gpu_crossover_same_machine.csv", header, rows)
    md_table("GPU vs CPU M3 on the same machine (Core Ultra 9 185H + RTX 4050; Milestone 4 data)",
             header, rows, "CPU/GPU > 1 means the GPU is faster. Crossover T is interpolated between "
             "measured thread counts.")

    # Stripe-size crossover for m=2 (sizes 8, 32, 128 MiB).
    srows = []
    for s in (1, 2, 3):
        pts = []
        for mib in (8, 32, 128):
            g = gpu.get((mib, 2, s))
            if g:
                bestcpu = min(g[f"cpu_l3_T{T}"] for T in (1, 2, 4, 8))
                pts.append((mib, bestcpu / min(g["gpu_table"], g["gpu_scalar"])))
        cross = None
        for (a, ra), (b, rb) in zip(pts, pts[1:]):
            if (ra - 1) * (rb - 1) < 0:
                la, lb = math.log2(a), math.log2(b)
                cross = 2 ** (la + (1 - ra) / (rb - ra) * (lb - la))
        srows.append([2, s, W_OF_S[s]] + [f2(r) for _, r in pts] +
                     [f"≈{cross:.0f} MiB" if cross else "none in 8–128 MiB"])
    md_table("GPU advantage vs stripe size (m=2; best CPU thread count vs best GPU kernel)",
             ["m", "s", "w", "8 MiB", "32 MiB", "128 MiB", "GPU stops winning above"], srows,
             "Values are best-CPU time / GPU time; >1 means the GPU is faster.")

    # (2) Projection onto this machine's fine-grained thread sweep.
    prow = []
    for m, s in MS:
        g = gpu.get((32, m, s))
        if not g:
            continue
        best_gpu = min(g["gpu_table"], g["gpu_scalar"])
        scale = sc.t("M3 gPPM SIMD", m, s, 1) / g["cpu_l3_T1"]
        proj = best_gpu * scale
        row = [m, s, W_OF_S[s], f2(best_gpu), f2(scale, 3), f2(proj)]
        for src, label in ((sc, "M2 PPM (scalar)"), (sc, "M3 gPPM SIMD"), (pinned, "M3 gPPM SIMD")):
            if src is None:
                continue
            c = src.curve(label, m, s)
            Tx = crossover(c, proj)
            best = min(t for _, t in c)
            row += [f2(best), "CPU wins at T=1" if Tx == 1.0 else
                    ("GPU wins at every T" if Tx is None else f"T ≥ {math.ceil(Tx - 1e-9)} (≈{Tx:.1f})")]
        prow.append(row)
    header = ["m", "s", "w", "GPU ms (RTX 4050)", "machine scale", "GPU ms projected",
              "M2 best ms", "M2 needs", "M3 best ms", "M3 needs"] + (
                  ["M3 pinned best ms", "M3 pinned needs"] if pinned else [])
    write_csv("gpu_crossover_projected.csv", header, prow)
    md_table("Threads this CPU needs to beat the GPU (projected)", header, prow,
             "GPU time is scaled by (M3 T=1 here) / (M3 T=1 on the GPU machine) to cancel the "
             "single-core difference between the two CPUs. 'Needs T ≥ k' = with fewer than k threads "
             "the GPU is faster.")
    plot_gpu_vs_threads(gpu)
    plot_gpu_projected(sc, gpu, pinned)


def plot_gpu_vs_threads(gpu):
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for m, s in MS:
        g = gpu.get((32, m, s))
        if not g:
            continue
        best_gpu = min(g["gpu_table"], g["gpu_scalar"])
        Ts = (1, 2, 4, 8)
        ax.plot(Ts, [g[f"cpu_l3_T{T}"] / best_gpu for T in Ts], color=S_COLOR[s],
                linestyle=M_STYLE[m], marker=M_MARK[m], label=f"m={m}, s={s} (w={W_OF_S[s]})")
    ax.axhline(1, color=INK, linewidth=1)
    ax.text(1.02, 1.04, "above: GPU faster / below: CPU faster", fontsize=7, color=INK2)
    ax.set_xscale("log", base=2)
    ax.set_xticks([1, 2, 4, 8], ["1", "2", "4", "8"])
    ax.set_yscale("log")
    ax.set_xlabel("CPU M3 threads T")
    ax.set_ylabel("GPU speedup over CPU (CPU ms / GPU ms)")
    ax.set_title("GPU advantage shrinks as CPU threads increase (RTX 4050 machine, 32 MiB)")
    ax.legend(loc="upper left", bbox_to_anchor=(1.01, 1), fontsize=7)
    save(fig, "gpu_speedup_vs_cpu_threads")


# --------------------------------------------------------------------------- ablation
def ablation(sc):
    layers = [
        ("M1 sequential scalar", "M1 sequential (scalar)", 1),
        ("+ PPM partition, fixed sequence (M2, T=1)", "M2 PPM (scalar)", 1),
        ("+ SIMD region arithmetic (T=1)", "M2 PPM + SIMD", 1),
        ("+ gPPM cost-selected sequence (T=1)", "M3 gPPM SIMD, serial remainder", 1),
        ("+ 4 threads, independent groups only", "M3 gPPM SIMD, serial remainder", 4),
        ("+ parallel dependent remainder (T=4)", "M3 gPPM SIMD", 4),
        ("+ 8 threads", "M3 gPPM SIMD", 8),
    ]
    header = ["layer"] + [f"m{m}s{s}" for m, s in MS] + ["geomean ×"]
    body, csvrows = [], []
    for name, label, T in layers:
        vals = []
        for m, s in MS:
            t = sc.t(label, m, s, T)
            vals.append(sc.t("M1 sequential (scalar)", m, s) / t if t else None)
        gm = math.exp(statistics.mean(math.log(v) for v in vals if v)) if all(vals) else None
        body.append([name] + [f2(v) + "×" for v in vals] + [f2(gm) + "×"])
        csvrows.append([name] + [f2(v, 4) for v in vals] + [f2(gm, 4)])
    write_csv("ablation_layers.csv", header, csvrows)
    md_table("Ablation A — cumulative layers (speedup over M1 sequential scalar; 32 MiB, n=r=16)",
             header, body, "Each row adds one technique on top of the previous row.")

    # Leave-one-out from the full M3 configuration at T=8.
    full = ("M3 gPPM SIMD", 8)
    loo = [("full M3 (gPPM + SIMD + parallel remainder, T=8)", full),
           ("− SIMD", ("M3 gPPM scalar", 8)),
           ("− parallel remainder", ("M3 gPPM SIMD, serial remainder", 8)),
           ("− gPPM sequence selection (fixed PPM)", ("M2 PPM + SIMD", 8)),
           ("− multithreading (T=1)", ("M3 gPPM SIMD", 1)),
           ("− partitioning entirely (sequential SD, SIMD)", ("SD baseline (SIMD)", 1))]
    body = []
    for name, (label, T) in loo:
        vals = []
        for m, s in MS:
            vals.append(sc.t(label, m, s, T) / sc.t(full[0], m, s, full[1]))
        gm = math.exp(statistics.mean(math.log(v) for v in vals))
        body.append([name] + [f2(v) + "×" for v in vals] + [f2(gm) + "×"])
    md_table("Ablation B — leave one technique out (time relative to full M3 at T=8; >1 = slower)",
             header, body)

    # Full factorial: arithmetic × sequence/remainder × threads, per configuration.
    combos = [("scalar", "fixed PPM", "serial", "M2 PPM (scalar)"),
              ("scalar", "gPPM", "serial", "M3 gPPM scalar, serial remainder"),
              ("scalar", "gPPM", "parallel", "M3 gPPM scalar"),
              ("SIMD", "fixed PPM", "serial", "M2 PPM + SIMD"),
              ("SIMD", "gPPM", "serial", "M3 gPPM SIMD, serial remainder"),
              ("SIMD", "gPPM", "parallel", "M3 gPPM SIMD")]
    Tsel = [T for T in (1, 2, 4, 8, os.cpu_count() or 12) if T in sc.threads]
    frows, body = [], []
    for arith, seq, rem, label in combos:
        for T in Tsel:
            vals = [sc.t(label, m, s, T) for m, s in MS]
            frows.append([arith, seq, rem, T] + [f2(v, 4) for v in vals])
            gm = math.exp(statistics.mean(math.log(sc.t("M1 sequential (scalar)", m, s) / v)
                                          for (m, s), v in zip(MS, vals)))
            body.append([arith, seq, rem, T] + [f2(v) for v in vals] + [f2(gm) + "×"])
    write_csv("ablation_factorial.csv", ["arithmetic", "sequence", "remainder", "threads"] +
              [f"m{m}s{s}_ms" for m, s in MS], frows)
    md_table("Ablation C — full factorial (median ms; last column geomean speedup over M1)",
             ["arithmetic", "sequence", "remainder", "T"] + [f"m{m}s{s}" for m, s in MS] + ["geomean ×"],
             body)


def nr_analysis(rows):
    med = medians(rows)
    keyset = {(k[2], k[3], k[4], k[5]) for k in med}
    grid = {}
    for n, m, s, r in keyset:
        def g(build, impl, T, rem):
            for key, v in med.items():
                if key[:6] == (build, impl, n, m, s, r) and key[9] == T and key[10] == rem:
                    return v
        base = g("m2_simd", "baseline", 1, "serial")
        ppm1, ppm4 = g("m2_simd", "ppm", 1, "serial"), g("m2_simd", "ppm", 4, "serial")
        gp1, gp4 = g("m3_simd", "ppm", 1, "parallel"), g("m3_simd", "ppm", 4, "parallel")
        w = next(k[6] for k in med if k[2:6] == (n, m, s, r))
        grid[(n, m, s, r)] = dict(w=w, base=base, ppm1=ppm1, ppm4=ppm4, gp1=gp1, gp4=gp4)
    out = []
    for (n, m, s, r), d in sorted(grid.items()):
        b = d["base"]["ms"]
        out.append([n, m, s, r, d["w"], f2(b, 4), f2(d["ppm1"]["ms"], 4), f2(d["ppm4"]["ms"], 4),
                    f2(d["gp4"]["ms"], 4), f2(b / d["ppm4"]["ms"] - 1, 4),
                    f2(d["ppm4"]["ms"] / d["gp4"]["ms"] - 1, 4), f2(d["base"]["mibs"], 1),
                    f2(d["ppm4"]["mibs"], 1), f2(d["gp4"]["mibs"], 1)])
    write_csv("summary_nr.csv", ["n", "m", "s", "r", "w", "sd_ms", "ppm_T1_ms", "ppm_T4_ms", "gppm_T4_ms",
                                 "ppm_improvement", "gppm_over_ppm", "sd_MiB_s", "ppm_T4_MiB_s",
                                 "gppm_T4_MiB_s"], out)
    imp = {k: d["base"]["ms"] / d["ppm4"]["ms"] - 1 for k, d in grid.items()}
    # gPPM over PPM at T=1 isolates the calculation-sequence choice (paper Fig. 17);
    # at T=4 M3 also parallelizes the dependent remainder, which M2 cannot.
    gimp = {k: d["ppm1"]["ms"] / d["gp1"]["ms"] - 1 for k, d in grid.items()}
    gimp4 = {k: d["ppm4"]["ms"] / d["gp4"]["ms"] - 1 for k, d in grid.items()}

    ns = sorted({k[0] for k in grid})
    rs = sorted({k[3] for k in grid})
    for m, s in MS:
        body = [[f"n={n}"] + [pct(imp.get((n, m, s, r))) for r in rs] for n in ns]
        TABLES.append("")
        md_table(f"Ablation D — PPM improvement over SD (T=4), m={m}, s={s}: n × r",
                 ["", *[f"r={r}" for r in rs]], body)
    # By field width.
    body = []
    for w in (8, 16, 32):
        for m in (1, 2, 3):
            v = [imp[k] for k, d in grid.items() if d["w"] == w and k[1] == m]
            gv = [gimp[k] for k, d in grid.items() if d["w"] == w and k[1] == m]
            g4 = [gimp4[k] for k, d in grid.items() if d["w"] == w and k[1] == m]
            if v:
                body.append([w, m, len(v), pct(statistics.mean(v)), pct(min(v)), pct(max(v)),
                             pct(statistics.mean(gv)), f"{sum(x > 0.05 for x in gv)}/{len(gv)}",
                             pct(statistics.mean(g4))])
    md_table("Ablation E — effect of field width w (n,r ∈ {4..24}; T=4)",
             ["w", "m", "configs", "PPM mean (T=4)", "PPM min", "PPM max", "gPPM over PPM, T=1",
              "configs gPPM >5% faster, T=1", "M3 over M2, T=4 (incl. parallel remainder)"], body,
             "w is not a free knob: the published FAST coefficients fix GF(2^w) for each (n,m,s,r). "
             "Rows group the measured grid by the field the coefficients use.")
    plot_fig11(grid)
    plot_fig17(gimp, grid)
    return imp, gimp, grid


def plot_fig11(grid):
    for axis, fixed in (("n", "r"), ("r", "n")):
        fig, axes = plt.subplots(1, 3, figsize=(11, 3.6), sharey=True)
        for m, ax in zip((1, 2, 3), axes):
            for s in (1, 2, 3):
                xs = sorted({k[0] if axis == "n" else k[3] for k in grid})
                keys = [(x, m, s, 16) if axis == "n" else (16, m, s, x) for x in xs]
                keys = [k for k in keys if k in grid]
                xv = [k[0] if axis == "n" else k[3] for k in keys]
                ax.plot(xv, [grid[k]["ppm4"]["mibs"] for k in keys], color=S_COLOR[s], marker="o",
                        label=f"PPM s={s}")
                ax.plot(xv, [grid[k]["base"]["mibs"] for k in keys], color=S_COLOR[s], marker="o",
                        linestyle=(0, (4, 2)), mfc="white", label=f"SD s={s}")
            ax.set_title(f"m={m}  ({fixed}=16, T=4, 32 MiB)")
            ax.set_xlabel(axis)
            ax.set_yscale("log")
        axes[0].set_ylabel("decoding speed (MiB/s, log)")
        axes[0].legend(fontsize=7, ncol=2)
        fig.suptitle(f"Paper Fig. 11 reproduction — decoding speed vs {axis}: PPM (solid) vs SD (dashed)",
                     fontweight="bold")
        fig.tight_layout()
        save(fig, f"paper_fig11_speed_vs_{axis}")


def plot_fig17(gimp, grid):
    vals = sorted(gimp.values())
    fig, ax = plt.subplots(figsize=(6.4, 3.8))
    ax.bar(range(len(vals)), [v * 100 for v in vals], width=1.0,
           color=[ORANGE if v > 0.05 else BLUE for v in vals])
    ax.axhline(0, color=INK, linewidth=0.8)
    ax.set_xlabel(f"SD configuration (sorted; {len(vals)} measured)")
    ax.set_ylabel("gPPM improvement over PPM (%)")
    ax.set_title("Paper Fig. 17 reproduction — gPPM over PPM (measured, T=1, n,r ∈ {4..24})")
    ax.legend(handles=[Line2D([], [], color=ORANGE, lw=6, label=">5% faster with gPPM"),
                       Line2D([], [], color=BLUE, lw=6, label="within ±5% / slower")], fontsize=7)
    save(fig, "paper_fig17_gppm_over_ppm_measured")


def plot_fig4_ops():
    base = {(r["n"], r["m"], r["s"], r["r"], r["z"]): int(r["C1"])
            for r in read(os.path.join(ROOT, "results", "sweep_results.csv")) if r["C1"]}
    ppm = {(r["n"], r["m"], r["s"], r["r"], r["z"]): int(r["C_t1"])
           for r in read(os.path.join(ROOT, "results", "ppm_sweep_results.csv")) if r["C_t1"]}
    gp = {(r["n"], r["m"], r["s"], r["r"], r["z"]): int(r["C_t1"])
          for r in read(os.path.join(ROOT, "results", "m3_sweep_results.csv")) if r["C_t1"]}
    keys = [k for k in base if k in ppm and k in gp]
    if not keys:
        return None
    fig, ax = plt.subplots(figsize=(7.2, 3.8))
    labels, pd, gd = [], [], []
    for m, s in MS:
        ks = [k for k in keys if int(k[1]) == m and int(k[2]) == s]
        labels.append(f"m{m}s{s}")
        pd.append([100 * (1 - ppm[k] / base[k]) for k in ks])
        gd.append([100 * (1 - gp[k] / base[k]) for k in ks])
    pos = range(len(labels))
    b1 = ax.boxplot(pd, positions=[p - 0.18 for p in pos], widths=0.3, patch_artist=True,
                    showfliers=False, medianprops={"color": INK})
    b2 = ax.boxplot(gd, positions=[p + 0.18 for p in pos], widths=0.3, patch_artist=True,
                    showfliers=False, medianprops={"color": INK})
    for b, c in ((b1, BLUE), (b2, ORANGE)):
        for p in b["boxes"]:
            p.set_facecolor(c)
            p.set_edgecolor(c)
    ax.set_xticks(list(pos), labels)
    ax.set_ylabel("multiply-XOR operations removed (%)")
    ax.set_title(f"Paper Figs. 4–6 analogue — multiply-XOR calls removed vs sequential SD "
                 f"({len(keys)} configurations)")
    ax.legend([b1["boxes"][0], b2["boxes"][0]], ["PPM (fixed sequence)", "gPPM (cost-selected)"], fontsize=7)
    save(fig, "paper_fig4_cost_reduction")
    improved = sum(gp[k] < ppm[k] for k in keys)
    gains = [ppm[k] / gp[k] - 1 for k in keys]
    return {"configs": len(keys), "gppm_fewer_ops": improved, "max_ops_gain": max(gains),
            "ppm_mean_reduction": statistics.mean(1 - ppm[k] / base[k] for k in keys)}


def fig10(sc):
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    body = []
    for m, s in MS:
        base = sc.t("SD baseline (SIMD)", m, s)
        c = sc.curve("M2 PPM + SIMD", m, s)
        ax.plot([T for T, _ in c], [100 * (base / t - 1) for _, t in c], color=S_COLOR[s],
                linestyle=M_STYLE[m], marker=M_MARK[m], label=f"m={m}, s={s}")
        d = dict(c)
        body.append([m, s, W_OF_S[s]] + [pct(base / d[T] - 1) if T in d else "—" for T in (1, 2, 3, 4, 6, 8, 12)])
    ax.axhline(0, color=INK, linewidth=0.8)
    ax.set_xlabel("threads T")
    ax.set_ylabel("improvement (PPM / SD − 1), %")
    ax.set_title("Paper Fig. 10 reproduction — PPM improvement vs T (32 MiB, r=16, z=1)")
    ax.legend(loc="upper left", bbox_to_anchor=(1.01, 1), fontsize=7)
    save(fig, "paper_fig10_improvement_vs_threads")
    md_table("Paper Fig. 10 reproduction — PPM improvement over SD (both SIMD)",
             ["m", "s", "w"] + [f"T={T}" for T in (1, 2, 3, 4, 6, 8, 12)], body)


def stripe_analysis(rows):
    med = medians(rows)
    sizes = sorted({k[8] for k in med})
    body, fig_data = [], defaultdict(list)
    for m, s in MS:
        row = [m, s, W_OF_S[s]]
        for mib in sizes:
            def g(build, impl, T, rem):
                return med.get((build, impl, 16, m, s, 16, W_OF_S[s], 1, mib, T, rem))
            b, p4, g4 = g("m2_simd", "baseline", 1, "serial"), g("m2_simd", "ppm", 4, "serial"), \
                g("m3_simd", "ppm", 4, "parallel")
            if b and p4:
                row.append(pct(b["ms"] / p4["ms"] - 1))
                fig_data[(m, s)].append((mib, b["ms"] / p4["ms"] - 1, b["ms"] / g4["ms"] - 1 if g4 else None))
            else:
                row.append("—")
        body.append(row)
    md_table("Ablation F — PPM improvement over SD vs stripe size (T=4; paper Fig. 13)",
             ["m", "s", "w"] + [f"{x} MiB" for x in sizes], body)
    fig, axes = plt.subplots(1, 2, figsize=(11, 3.8), sharey=True)
    for idx, (ax, title) in enumerate(zip(axes, ("PPM (M2 + SIMD)", "gPPM (M3)"))):
        for (m, s), pts in fig_data.items():
            ax.plot([p[0] for p in pts], [100 * p[1 + idx] for p in pts if p[1 + idx] is not None],
                    color=S_COLOR[s], linestyle=M_STYLE[m], marker=M_MARK[m], label=f"m={m}, s={s}")
        ax.set_xscale("log", base=2)
        ax.set_xlabel("stripe size (MiB)")
        ax.set_title(f"{title}, T=4")
        ax.axhline(0, color=INK, linewidth=0.8)
    axes[0].set_ylabel("improvement over SD (%)")
    axes[1].legend(loc="upper left", bbox_to_anchor=(1.01, 1), fontsize=7)
    fig.suptitle("Paper Fig. 13 reproduction — improvement vs stripe size", fontweight="bold")
    fig.tight_layout()
    save(fig, "paper_fig13_improvement_vs_stripe")


def z_analysis(rows):
    med = medians(rows)
    body = []
    for m in (1, 2, 3):
        for s in (2, 3):
            for z in range(1, s + 1):
                def g(impl, T, rem):
                    return med.get(("m3_simd", impl, 16, m, s, 16, W_OF_S[s], z, 32, T, rem))
                b = g("baseline", 1, "serial")
                if not b:
                    continue
                vals = [g("ppm", T, "parallel") for T in (1, 4, 8)]
                body.append([m, s, z, f2(b["ms"])] + [f2(v["ms"]) for v in vals] +
                            [pct(b["ms"] / v["ms"] - 1) for v in vals] + [vals[0]["part"]])
    md_table("Ablation G — sector failures spread over z rows (M3, 32 MiB, n=r=16)",
             ["m", "s", "z", "SD ms", "M3 T1 ms", "M3 T4 ms", "M3 T8 ms", "impr. T1", "impr. T4",
              "impr. T8", "partitioned"], body,
             "Larger z leaves fewer independent rows (p = r − z), so less work is parallel.")


def pinned_analysis(sc, pinned):
    """Unpinned (OS-scheduled) vs P-core-first pinned thread placement."""
    impls = [("M2 PPM (scalar)", BLUE), ("M2 PPM + SIMD", YELLOW), ("M3 gPPM SIMD", ORANGE)]
    fig, axes = plt.subplots(3, 3, figsize=(10.5, 8.4), sharex=True)
    body = []
    for (m, s), ax in zip(MS, axes.flat):
        for label, color in impls:
            for src, ls, name in ((sc, (0, (4, 2)), "unpinned"), (pinned, (0, ()), "pinned")):
                c = [p for p in src.curve(label, m, s) if p[0] <= (os.cpu_count() or 12)]
                if len(c) < 3:
                    continue
                ax.plot([T for T, _ in c], [c[0][1] / t for _, t in c], color=color, linestyle=ls,
                        marker="o" if name == "pinned" else None, label=f"{label}, {name}")
            c = pinned.curve(label, m, s)
            if len(c) >= 3:
                tp = taper(c)
                body.append([label, m, s, W_OF_S[s], tp["peak_T"], f2(tp["peak"]), tp["knee"], tp["t90"],
                             tp["eff50"] or "—"])
        for x in (4.5, 8.5):
            ax.axvline(x, color=INK2, linewidth=0.7)
        for x, t in ((2.5, "P"), (6.5, "E"), (10.5, "HT")):
            ax.text(x, 0.02, t, transform=ax.get_xaxis_transform(), ha="center", fontsize=7, color=INK2)
        ax.set_title(f"m={m}, s={s} (GF(2^{W_OF_S[s]}))")
        ax.set_ylim(bottom=0)
        ax.set_xticks(range(1, 13))
    for ax in axes[-1]:
        ax.set_xlabel("threads T")
    for ax in axes[:, 0]:
        ax.set_ylabel("speedup vs own T=1")
    handles, labels = axes.flat[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=3, bbox_to_anchor=(0.5, -0.06), fontsize=8)
    fig.suptitle("Thread placement: pinned (solid; T1–4 P-cores, T5–8 E-cores, T9–12 HT siblings) vs "
                 "OS-scheduled (dashed)",
                 fontweight="bold")
    fig.tight_layout()
    save(fig, "scaling_pinned_vs_unpinned")
    md_table("Taper with pinned placement (threads 1–4 on P-cores, 5–8 on E-cores, 9–12 on HT siblings)",
             ["implementation", "m", "s", "w", "peak T", "peak speedup", "knee T", "T at 90% of peak",
              "first T eff.<50%"], body)


def headline(sc, imp, gimp, ops):
    t2 = []
    for m, s in MS:
        b = sc.t("SD baseline (SIMD)", m, s)
        t2.append(b / sc.t("M2 PPM + SIMD", m, s, 2) - 1)
    vals = list(imp.values())
    rows = [
        ["PPM max improvement over SD", "210.81%", pct(max(vals)), "n×r grid, T=4"],
        ["PPM mean improvement over SD", "61.09% (8.22%–210.81%)",
         f"{pct(statistics.mean(vals))} ({pct(min(vals))} – {pct(max(vals))})", "n×r grid, T=4"],
        ["PPM mean improvement at T=2", "46.29% (8.45%–178.38%)",
         f"{pct(statistics.mean(t2))} ({pct(min(t2))} – {pct(max(t2))})", "n=r=16, 9 (m,s)"],
        ["gPPM over PPM, max", "102.41%", pct(max(gimp.values())), "n×r grid, T=1"],
        ["share of configs where gPPM clearly beats PPM", "≈1/3",
         f"{sum(v > 0.05 for v in gimp.values())}/{len(gimp)} (>5%)", "n×r grid, T=1"],
    ]
    if ops:
        rows.append(["configs where gPPM needs fewer operations than PPM", "≈1/3 of 1323 improved",
                     f"{ops['gppm_fewer_ops']}/{ops['configs']}", "operation counts, full sweep"])
    TABLES.insert(0, "")
    md_table("Paper headline numbers vs this reproduction", ["metric", "paper (E5-2603, T≤4)",
                                                             "ours (i5-13420H)", "scope"], rows)
    TABLES.insert(0, TABLES.pop())


def main():
    global MS
    style()
    os.makedirs(OUT, exist_ok=True)
    gpu = load_gpu()
    sc = Scaling(read(os.path.join(RAW, "scaling.csv")))
    pr = read(os.path.join(RAW, "scaling_pinned.csv"))
    pinned = Scaling(pr) if pr else None
    MS = [ms for ms in MS if sc.t("M3 gPPM SIMD", *ms, 1) and sc.t("M1 sequential (scalar)", *ms)]
    scaling_analysis(sc, gpu)
    plot_combined(sc, None, gpu, pinned)
    gpu_analysis(sc, gpu, pinned)
    ablation(sc)
    fig10(sc)
    if pinned:
        pinned_analysis(sc, pinned)
    imp = gimp = None
    nr_rows = read(os.path.join(RAW, "nr.csv"))
    if nr_rows:
        imp, gimp, _ = nr_analysis(nr_rows)
    st = read(os.path.join(RAW, "stripe.csv"))
    if st:
        stripe_analysis(st)
    zr = read(os.path.join(RAW, "z.csv"))
    if zr:
        z_analysis(zr)
    ops = plot_fig4_ops()
    if imp:
        headline(sc, imp, gimp, ops)
    with open(os.path.join(OUT, "tables.md"), "w") as f:
        f.write("# Evaluation tables (generated by scripts/analyze_evaluation.py)\n\n")
        f.write("\n".join(TABLES))
    print("wrote", os.path.join(OUT, "tables.md"))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Validate retained M4 trials; derive tables, plots and profiler summaries."""
import csv
from collections import defaultdict
import json
from pathlib import Path
import re
import sqlite3
import statistics as st

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "results/milestone4"


def write_csv(path, rows):
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def union_ns(intervals):
    total = 0
    end = -1
    for start, stop in sorted(intervals):
        total += max(0, stop - max(start, end))
        end = max(end, stop)
    return total


def profile_summaries():
    cpu, gpu = [], []
    for s in (1, 2, 3):
        text = (OUT / f"profiles/cpu_m2_s{s}_annotated.txt").read_text()
        total = int(re.search(r"([\d,]+) \(100\.0%\)\s+PROGRAM TOTALS", text)[1].replace(",", ""))
        # Only the self-cost function summary, before source annotations.
        summary = text.split("-- Auto-annotated source:")[0]
        functions = defaultdict(int)
        for count, func in re.findall(r"^\s*([\d,]+)\s+\([\d. ]+%\)\s+\S+:(\S+)", summary, re.M):
            functions[func] += int(count.replace(",", ""))
        region = sum(n for f, n in functions.items() if f.startswith(("mult_xors_", "xor_region_")))
        mul32 = sum(n for f, n in functions.items() if f.startswith("gf_mul32"))
        cpu.append({"m": 2, "s": s, "w": (8, 16, 32)[s-1],
                    "instructions": total, "region_self_instructions": region,
                    "region_self_percent": 100 * region / total,
                    "gf_mul32_self_instructions": mul32,
                    "gf_mul32_self_percent": 100 * mul32 / total})
        for variant in ("gpu_scalar", "gpu_table"):
            with sqlite3.connect(OUT / f"profiles/{variant}_m2_s{s}.sqlite") as db:
                kernels = db.execute("SELECT start,end FROM CUPTI_ACTIVITY_KIND_KERNEL").fetchall()
                copies = db.execute("SELECT start,end,bytes,copyKind FROM CUPTI_ACTIVITY_KIND_MEMCPY").fetchall()
                apis = db.execute("SELECT s.value,r.end-r.start FROM CUPTI_ACTIVITY_KIND_RUNTIME r "
                                  "JOIN StringIds s ON r.nameId=s.id").fetchall()
                names = db.execute("SELECT s.value FROM CUPTI_ACTIVITY_KIND_KERNEL k "
                                   "JOIN StringIds s ON k.shortName=s.id").fetchall()
                assert kernels and all("matrix_vector_kernel" in n[0] for n in names), "Warm-up leaked into GPU capture"
                by_api = defaultdict(lambda: [0, 0])
                for name, duration in apis:
                    by_api[name][0] += 1
                    by_api[name][1] += duration
                api_rows = [{"api": name, "calls": v[0], "total_ms": v[1]/1e6}
                            for name, v in sorted(by_api.items(), key=lambda x: -x[1][1])]
                write_csv(OUT / f"profiles/{variant}_m2_s{s}_api.csv", api_rows)
                gpu.append({"implementation": variant, "m": 2, "s": s,
                            "w": (8, 16, 32)[s-1], "kernels": len(kernels),
                            "kernel_sum_ms": sum(b-a for a,b in kernels)/1e6,
                            "kernel_union_ms": union_ns(kernels)/1e6,
                            "copy_sum_ms": sum(b-a for a,b,_,_ in copies)/1e6,
                            "copy_bytes": sum(n for _,_,n,_ in copies),
                            "copy_count": len(copies),
                            "stream_sync_api_ms": sum(d for n,d in apis if n.startswith("cudaStreamSynchronize"))/1e6,
                            "allocation_free_api_ms": sum(d for n,d in apis if n.startswith(("cudaMalloc", "cudaFree")))/1e6,
                            "launch_api_ms": sum(d for n,d in apis if n.startswith("cudaLaunchKernel"))/1e6})
    write_csv(OUT / "cpu_profile_summary.csv", cpu)
    write_csv(OUT / "gpu_profile_summary.csv", gpu)
    return cpu, gpu


def main():
    groups = defaultdict(list)
    checksums = defaultdict(set)
    order = defaultdict(list)
    count = 0
    for name, expected in (("final_32mib", 540), ("size_8mib", 180), ("size_128mib", 180)):
        with (OUT / f"{name}.csv").open() as f:
            rows = list(csv.DictReader(f))
        assert len(rows) == expected, (name, len(rows))
        for r in rows:
            assert r["status"] == "PASS" and r["measurement"] == "native"
            size, m, s, t = (int(r[k]) for k in ("stripe_mib", "m", "s", "threads"))
            assert int(r["w"]) == (8,16,32)[s-1]
            assert float(r["seconds"]) > 0 and int(r["used_partitioning"]) == 1
            assert int(r["effective_threads"]) == t
            key = (size, m, s, r["implementation"], t)
            groups[key].append(r)
            checksums[size,m,s].add(r["checksum"])
            order[size,m,s,int(r["trial"])].append(int(r["order_position"]))
        count += len(rows)
    assert all(len(h) == 1 for h in checksums.values())
    assert all(sorted(p) == list(range(1,7)) for p in order.values())
    summaries = []
    stats = {}
    for key, rows in sorted(groups.items()):
        assert len(rows) == 10
        assert sorted(int(r["trial"]) for r in rows) == list(range(1,11))
        assert len({r["logical_mult_xors"] for r in rows}) == 1
        values = np.array([float(r["seconds"]) * 1000 for r in rows])
        median = float(np.median(values))
        size, m, s, variant, t = key
        stats[key] = median
        summaries.append(dict(stripe_mib=size, m=m, s=s, w=(8,16,32)[s-1],
                              implementation=variant, threads=t, trials=10,
                              median_ms=median, q25_ms=float(np.percentile(values,25)),
                              q75_ms=float(np.percentile(values,75)),
                              min_ms=float(min(values)), max_ms=float(max(values)),
                              median_data_MiB_s=st.median(float(r["data_MiB_s"]) for r in rows)))
    write_csv(OUT / "benchmark_summary.csv", summaries)
    comparison = []
    for size,m,s in sorted(checksums):
        cpu = {t:stats[size,m,s,"cpu_l3",t] for t in (1,2,4,8)}
        best = min(cpu, key=cpu.get)
        table = stats[size,m,s,"gpu_table",0]
        scalar = stats[size,m,s,"gpu_scalar",0]
        comparison.append(dict(stripe_mib=size,m=m,s=s,w=(8,16,32)[s-1],
                               best_cpu_threads=best,best_cpu_ms=cpu[best],
                               cpu_1t_ms=cpu[1], cpu_2t_ms=cpu[2], cpu_4t_ms=cpu[4],cpu_8t_ms=cpu[8],
                               gpu_scalar_ms=scalar,gpu_table_ms=table,
                               gpu_table_speedup_over_best_cpu=cpu[best]/table,
                               cpu_best_speedup_over_1t=cpu[1]/cpu[best],
                               cpu_best_efficiency=cpu[1]/cpu[best]/best))
    write_csv(OUT / "comparison_summary.csv", comparison)
    final = [r for r in comparison if r["stripe_mib"] == 32]
    profile_count = 0
    for path in sorted((OUT / "profiles").glob("*.csv")):
        if not re.match(r"(?:cpu|gpu_scalar|gpu_table)_m2_s[123]\.csv$", path.name):
            continue
        with path.open() as f:
            rows = list(csv.DictReader(f))
        assert len(rows) == 1 and rows[0]["status"] == "PASS"
        r = rows[0]
        assert r["measurement"] == "profiled"
        key = (32,2,int(r["s"]),r["implementation"],int(r["threads"]))
        assert r["logical_mult_xors"] == groups[key][0]["logical_mult_xors"], (path,"profile changed logical work")
        profile_count += 1
    assert profile_count == 9
    cpu_profiles, gpu_profiles = profile_summaries()
    with (OUT / "final_table.tex").open("w") as f:
        for r in final:
            f.write(f'{r["m"]}&{r["w"]}&{r["cpu_1t_ms"]:.3f}&{r["cpu_2t_ms"]:.3f}&'
                    f'{r["cpu_4t_ms"]:.3f}&{r["cpu_8t_ms"]:.3f}&'
                    f'{r["gpu_scalar_ms"]:.3f}&{r["gpu_table_ms"]:.3f}&'
                    f'{r["best_cpu_threads"]}&{r["gpu_table_speedup_over_best_cpu"]:.2f}$\\times$\\\\\n')
    with (OUT / "cpu_profile_table.tex").open("w") as f:
        for r in cpu_profiles:
            f.write(f'{r["w"]}&{r["instructions"]/1e6:.1f}&'
                    f'{r["region_self_percent"]:.1f}\\%&{r["gf_mul32_self_percent"]:.1f}\\%\\\\\n')
    with (OUT / "gpu_profile_table.tex").open("w") as f:
        for r in gpu_profiles:
            label = "Direct" if r["implementation"] == "gpu_scalar" else "Table"
            f.write(f'{r["w"]}&{label}&{r["kernels"]}&{r["kernel_union_ms"]:.2f}&'
                    f'{r["copy_sum_ms"]:.2f}&{r["stream_sync_api_ms"]:.2f}&'
                    f'{r["allocation_free_api_ms"]:.2f}\\\\\n')
    plt.rcParams.update({"font.size":11, "axes.labelsize":11, "legend.fontsize":9,
                         "svg.hashsalt":"milestone4", "savefig.bbox":"tight"})
    fig, axes = plt.subplots(1,3,figsize=(10.2,2.7),sharey=True)
    for s, ax in enumerate(axes,1):
        for m in (1,2,3):
            vals = [stats[32,m,s,"cpu_l3",t] for t in (1,2,4,8)]
            ax.plot((1,2,4,8), np.array(vals[0])/vals, marker="o",label=f"m={m}")
        ax.plot((1,2,4,8),(1,2,4,8),"--",color="0.6",label="Ideal")
        ax.set(title=f"GF($2^{{{(8,16,32)[s-1]}}}$), s={s}",xlabel="CPU threads",xticks=(1,2,4,8))
        ax.grid(alpha=.25); ax.legend(loc="upper left")
    axes[0].set_ylabel("Speedup over CPU L3 at 1 thread")
    fig.tight_layout()
    for ext in ("png","svg"): fig.savefig(OUT/f"cpu_thread_scaling.{ext}",dpi=240)
    plt.close(fig)
    fig, axes = plt.subplots(1,3,figsize=(10.2,2.7))
    for s,ax in enumerate(axes,1):
        sizes = (8,32,128)
        selected = [next(r for r in comparison if (r["stripe_mib"],r["m"],r["s"])==(b,2,s)) for b in sizes]
        for label,key,marker in (("Best CPU (1/2/4/8T)","best_cpu_ms","o"),
                                 ("GPU direct","gpu_scalar_ms","s"),("GPU table","gpu_table_ms","^")):
            ax.plot(sizes,[r[key] for r in selected],marker=marker,label=label)
        ax.set(xscale="log",yscale="log",xticks=sizes,
               title=f"GF($2^{{{(8,16,32)[s-1]}}}$), m=2, s={s}",xlabel="Stripe size (MiB)")
        ax.set_xticklabels(sizes); ax.grid(alpha=.25,which="both"); ax.legend()
    axes[0].set_ylabel("Median recovery latency (ms)")
    fig.tight_layout()
    for ext in ("png","svg"): fig.savefig(OUT/f"stripe_size_scaling.{ext}",dpi=240)
    plt.close(fig)
    report = {"validated_native_trials":count,"validated_profiled_trials":profile_count,
              "comparison_32mib":final,"cpu_profiles":cpu_profiles,"gpu_profiles":gpu_profiles}
    (OUT/"analysis.json").write_text(json.dumps(report,indent=2)+"\n")
    print(f"Validated {count} native trials; wrote summaries, TeX tables, and scaling plots.")
    print("m w | CPU1 CPU2 CPU4 CPU8 | GPU-direct GPU-table | best-T | table/best CPU speedup")
    for r in final:
        print(f'{r["m"]} {r["w"]} | '+" ".join(f'{r[f"cpu_{t}t_ms"]:.3f}' for t in (1,2,4,8))+
              f' | {r["gpu_scalar_ms"]:.3f} {r["gpu_table_ms"]:.3f} | {r["best_cpu_threads"]} | '
              f'{r["gpu_table_speedup_over_best_cpu"]:.3f}x')


if __name__ == "__main__":
    main()

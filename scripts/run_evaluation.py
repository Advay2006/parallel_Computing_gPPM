#!/usr/bin/env python3
"""Run the evaluation experiments (thread scaling, ablation, paper reproductions).

Raw per-trial CSVs go to results/evaluation/raw/.  Each experiment is resumable:
an experiment whose CSV already exists is skipped, so an interrupted run can be
restarted without mixing sessions inside one file (delete the partial CSV first).

Usage: python3 scripts/run_evaluation.py [experiment ...]
Experiments: scaling, nr, stripe, z, scaling_pinned   (default: all)
"""
import json
import os
import platform
import subprocess
import sys
import time
from datetime import datetime, timezone

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "results", "evaluation")
RAW = os.path.join(OUT, "raw")
BUILD = os.path.join(ROOT, "build")
MS = [(m, s) for m in (1, 2, 3) for s in (1, 2, 3)]
NCPU = os.cpu_count() or 1
SCALING_THREADS = f"1-{NCPU},{NCPU + 2},{NCPU + 4}"


def run(binary, csv, env=None, **kw):
    cmd = [os.path.join(BUILD, binary), "--csv", csv, "--append"]
    for key, value in kw.items():
        flag = "--" + key.replace("_", "-")
        if value is True:
            cmd.append(flag)
        else:
            cmd += [flag, str(value)]
    res = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                         env={**os.environ, **(env or {})})
    if res.returncode:
        sys.exit(f"FAILED: {' '.join(cmd)}\n{res.stderr}")
    print(res.stdout.strip(), flush=True)


def experiment(name, body):
    csv = os.path.join(RAW, f"{name}.csv")
    if os.path.exists(csv):
        print(f"skip {name}: {csv} exists")
        return
    tmp = csv + ".partial"
    if os.path.exists(tmp):
        os.remove(tmp)
    start = time.time()
    body(tmp)
    os.rename(tmp, csv)
    print(f"done {name} in {time.time() - start:.0f} s", flush=True)


def scaling(csv):
    # Thread scaling and layer ablation at the paper's default n=r=16, z=1, 32 MiB.
    for m, s in MS:
        for binary, remainder in (("eval_m2_scalar", "serial"), ("eval_m2_simd", "serial"),
                                  ("eval_m3_scalar", "both"), ("eval_m3_simd", "both")):
            run(binary, csv, m=m, s=s, threads=SCALING_THREADS, remainder=remainder,
                baseline=True, trials=10, tag="scaling")


# Hybrid CPU control run: one thread per P-core first, then E-cores, then the
# P-cores' Hyper-Threading siblings (i5-13420H: CPUs 0-7 = 4 P-cores x 2, 8-11 = E-cores).
PINNED = {"OMP_PLACES": "{0},{2},{4},{6},{8},{9},{10},{11},{1},{3},{5},{7}",
          "OMP_PROC_BIND": "close"}


def scaling_pinned(csv):
    for m, s in MS:
        for binary, remainder in (("eval_m2_scalar", "serial"), ("eval_m2_simd", "serial"),
                                  ("eval_m3_simd", "parallel")):
            run(binary, csv, env=PINNED, m=m, s=s, threads=f"1-{NCPU}", remainder=remainder,
                baseline=True, trials=10, tag="pinned")


def infeasible():
    """(n, m, s, r) with no valid z=1 failure geometry, per the M3 correctness sweep."""
    import csv as _csv
    with open(os.path.join(ROOT, "results", "m3_sweep_results.csv"), newline="") as f:
        return {(int(r["n"]), int(r["m"]), int(r["s"]), int(r["r"]))
                for r in _csv.DictReader(f) if r["status"] == "SKIP" and r["z"] == "1"}


def nr(csv):
    # Paper Figs 11, 12 and 17: n and r grid at T=4 (paper default) plus T=1.
    skip = infeasible()
    for n in (4, 8, 12, 16, 20, 24):
        for r in (4, 8, 12, 16, 20, 24):
            for m, s in MS:
                if (n, m, s, r) in skip:
                    print(f"skip infeasible n={n} m={m} s={s} r={r}")
                    continue
                run("eval_m2_simd", csv, n=n, r=r, m=m, s=s, threads="1,4",
                    baseline=True, trials=5, tag="nr")
                run("eval_m3_simd", csv, n=n, r=r, m=m, s=s, threads="1,4",
                    remainder="parallel", trials=5, tag="nr")


def stripe(csv):
    # Paper Fig 13: stripe size at n=r=16.
    for mib in (1, 2, 4, 8, 16, 32, 64, 128):
        for m, s in MS:
            run("eval_m2_simd", csv, m=m, s=s, stripe_mib=mib, threads="1,4,8",
                baseline=True, trials=7, tag="stripe")
            run("eval_m3_simd", csv, m=m, s=s, stripe_mib=mib, threads="1,4,8",
                remainder="parallel", trials=7, tag="stripe")


def zrows(csv):
    # Sector failures spread over z rows (paper reports z=1 only).
    for m in (1, 2, 3):
        for s in (2, 3):
            for z in range(1, s + 1):
                run("eval_m3_simd", csv, m=m, s=s, z=z, threads="1,4,8",
                    remainder="parallel", baseline=True, trials=7, tag="z")


EXPERIMENTS = {"scaling": scaling, "nr": nr, "stripe": stripe, "z": zrows,
               "scaling_pinned": scaling_pinned}


def environment(names):
    def cmd(*c):
        try:
            return subprocess.run(c, capture_output=True, text=True).stdout.strip()
        except OSError:
            return None
    env = {
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "cpu_count": NCPU,
        "platform": platform.platform(),
        "lscpu": cmd("lscpu"),
        "gcc": cmd("gcc", "--version").splitlines()[0],
        "power_profile": cmd("cat", "/sys/firmware/acpi/platform_profile"),
        "loadavg_at_start": open("/proc/loadavg").read().strip(),
        "OMP_DYNAMIC": os.environ.get("OMP_DYNAMIC"),
        "OMP_PROC_BIND": os.environ.get("OMP_PROC_BIND", "unset"),
        "git_head": cmd("git", "-C", ROOT, "rev-parse", "HEAD"),
        "experiments": names,
    }
    path = os.path.join(OUT, "environment.json")
    if os.path.exists(path):
        path = os.path.join(OUT, f"environment_{'_'.join(names)}.json")
    with open(path, "w") as f:
        json.dump(env, f, indent=2)


def main():
    names = sys.argv[1:] or list(EXPERIMENTS)
    for name in names:
        if name not in EXPERIMENTS:
            sys.exit(f"unknown experiment {name}; choose from {list(EXPERIMENTS)}")
    os.makedirs(RAW, exist_ok=True)
    os.environ["OMP_DYNAMIC"] = "FALSE"
    environment(names)
    for name in names:
        experiment(name, EXPERIMENTS[name])


if __name__ == "__main__":
    main()

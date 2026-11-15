#!/usr/bin/env python3
"""Profile recovery-only ranges separately from native benchmark measurements."""
import csv
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "results/milestone4/profiles"


def run(command, log, env):
    print("Running:", " ".join(map(str, command)), flush=True)
    with (OUT / log).open("w") as f:
        subprocess.run(command, cwd=ROOT, env=env, stdout=f,
                       stderr=subprocess.STDOUT, check=True)


def main():
    for tool in ("valgrind", "callgrind_annotate", "nsys"):
        if not shutil.which(tool):
            raise SystemExit(f"Required profiling tool unavailable: {tool}")
    OUT.mkdir(parents=True, exist_ok=True)
    if any(OUT.iterdir()):
        raise SystemExit("Retained profiles already exist. Move the complete results/milestone4 "
                         "directory aside, then rerun the benchmark and profiles together.")
    env = dict(os.environ, OMP_DYNAMIC="FALSE")
    with (OUT.parent / "final_32mib.csv").open() as f:
        native = list(csv.DictReader(f))
    commands = []
    for s in (1, 2, 3):
        name = f"cpu_m2_s{s}"
        calibration = next(r["c_multiply"] for r in native if r["implementation"] == "cpu_l3"
                           and r["m"] == "2" and int(r["s"]) == s and r["threads"] == "1")
        cpu_env = dict(env, M4_CALIBRATION_C=calibration)
        marker = OUT / (name + "_calibration.json")
        # Instrumentation is disabled during encoding, warm-up, and validation.
        command = ["valgrind", "--tool=callgrind", "--instr-atstart=no",
                   f"--callgrind-out-file={OUT / (name + '.callgrind')}",
                   "./build/m4_profile_cpu", "--variant", "cpu", "--threads", "1",
                   "--m", "2", "--s", str(s), "--trials", "1",
                   "--profile", "callgrind", "--csv", str(OUT / (name + ".csv"))]
        commands.append(command)
        dump = OUT / (name + ".callgrind.1")
        run(command, name + ".log", cpu_env)
        marker.write_text(json.dumps({"M4_CALIBRATION_C": calibration,
                                     "source": "../final_32mib.csv, CPU L3 T=1, m=2"}, indent=2)+"\n")
        if not dump.exists():
            raise RuntimeError(f"Recovery-only Callgrind dump missing: {dump}")
        run(["callgrind_annotate", "--inclusive=no", "--threshold=99", str(dump)],
            name + "_annotated.txt", env)
        run(["callgrind_annotate", "--inclusive=yes", "--tree=both", "--auto=no",
             "--threshold=99", str(dump)], name + "_calltree.txt", env)
        for variant in ("gpu_scalar", "gpu_table"):
            name = f"{variant}_m2_s{s}"
            command = ["nsys", "profile", "--trace=cuda", "--sample=none",
                       "--cpuctxsw=none", "--capture-range=cudaProfilerApi",
                       "--capture-range-end=stop", "--kill=none", "--export=sqlite",
                       "--output", str(OUT / name), "./build/m4_benchmark",
                       "--variant", variant, "--m", "2", "--s", str(s),
                       "--trials", "1", "--profile", "cuda",
                       "--csv", str(OUT / (name + ".csv"))]
            commands.append(command)
            run(command, name + ".log", env)
            run(["nsys", "stats", "--report",
                 "cuda_api_sum,cuda_gpu_kern_sum,cuda_gpu_mem_time_sum,cuda_gpu_mem_size_sum",
                 "--format", "csv", str(OUT / (name + ".sqlite"))],
                name + "_stats.csv", env)
    (OUT / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")


if __name__ == "__main__":
    main()

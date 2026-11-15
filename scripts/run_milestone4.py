#!/usr/bin/env python3
"""Retain commands, environment, and native (unprofiled) final measurements."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "results/milestone4"


def capture(command):
    p = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, check=False)
    return {"command": command, "returncode": p.returncode, "output": p.stdout}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    if any(OUT.iterdir()):
        raise SystemExit("Retained benchmark results already exist. Move results/milestone4 "
                         "to a safe location before starting a fresh benchmark and profile run.")
    env = dict(os.environ, OMP_DYNAMIC="FALSE")
    metadata = {
        "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "trial_order_seed": 20261115,
        "data_seed": "0x243f6a88",
        "warmups_per_variant_and_configuration": 1,
        "trials_per_variant_and_configuration": 10,
        "OMP_DYNAMIC": "FALSE",
        "OMP_PROC_BIND": env.get("OMP_PROC_BIND", "unset"),
        "OMP_PLACES": env.get("OMP_PLACES", "unset"),
        "affinity": sorted(os.sched_getaffinity(0)),
        "system": [capture(c) for c in (
            ["uname", "-a"], ["lscpu"], ["gcc", "--version"],
            ["nvidia-smi"], ["git", "rev-parse", "HEAD"],
            ["git", "status", "--short"],
            ["perf", "stat", "-e", "cycles,instructions", "--", "true"])],
        "source_sha256": {
            str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for folder in ("src/milestone3", "src/milestone_gpu", "src/milestone4")
            for p in sorted((ROOT / folder).glob("*")) if p.is_file()
        },
        "runs": [],
    }
    for size, extra, name in (
        (32, [], "final_32mib"), (8, ["--m", "2"], "size_8mib"),
        (128, ["--m", "2"], "size_128mib"),
    ):
        command = ["./build/m4_benchmark", "--stripe-mib", str(size),
                   "--csv", f"results/milestone4/{name}.csv", *extra]
        metadata["runs"].append(command)
        (OUT / "environment.json").write_text(json.dumps(metadata, indent=2) + "\n")
        print("Running:", " ".join(command), flush=True)
        with (OUT / f"{name}.log").open("w") as log:
            subprocess.run(command, cwd=ROOT, env=env, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        print((OUT / f"{name}.log").read_text(), end="", flush=True)
    metadata["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    metadata["gpu_after"] = capture(["nvidia-smi"])
    (OUT / "environment.json").write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    main()

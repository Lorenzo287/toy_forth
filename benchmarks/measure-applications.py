#!/usr/bin/env python3
"""Measure application phases separately from fixture construction and checks.

Every Toy process checks its results. With --baseline, measured pairs alternate
execution order. --json includes raw samples as well as medians. This runner is
repository tooling; the example applications themselves only require Toy.
"""

import argparse
import json
from pathlib import Path
import platform
import statistics
import subprocess
import time


ROOT = Path(__file__).resolve().parents[1]
WORKLOADS = ("log-report", "particles")


def sample(executable, workload):
    start = time.perf_counter_ns()
    result = subprocess.run(
        [str(executable), "--file", str(ROOT / "benchmarks" / f"{workload}.toy")],
        capture_output=True, text=True, check=True, timeout=120,
    )
    wall_ms = (time.perf_counter_ns() - start) / 1e6
    if result.stderr:
        raise ValueError(result.stderr)
    phases = {workload + " process": {"wall": wall_ms}}
    for line in result.stdout.splitlines():
        tokens = line.split()
        if not tokens or tokens[0] != workload:
            raise ValueError(f"Unexpected benchmark output: {line!r}")
        fields = dict(token.split("=", 1) for token in tokens[1:])
        if "checksum" not in fields:
            raise ValueError(f"Missing checksum: {line!r}")
        case = " ".join([workload] + [f"{key}={value}" for key, value in fields.items()
                                    if not key.endswith("_ns") and key != "checksum"])
        values = {key[:-3]: int(value) / 1e6 for key, value in fields.items()
                  if key.endswith("_ns")}
        required = {"setup", "analyze", "report"} if workload == "log-report" else {"setup", "advance"}
        if values.keys() != required or any(value < 0 for value in values.values()):
            raise ValueError(f"Invalid phase timings: {line!r}")
        values["application"] = sum(value for key, value in values.items() if key != "setup")
        if case in phases:
            raise ValueError(f"Duplicate case: {case}")
        phases[case] = values
    if len(phases) != 7:
        raise ValueError(f"Expected six cases in {workload}, found {len(phases) - 1}")
    return phases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toy", type=Path, default=ROOT / "build/gcc/release/toy")
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--runs", type=int, default=7)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--workloads", nargs="+", choices=WORKLOADS, default=WORKLOADS)
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    executables = {"candidate": args.toy.resolve()}
    if args.baseline:
        executables["baseline"] = args.baseline.resolve()
    records, summaries = {}, []
    for workload in args.workloads:
        for executable in executables.values():
            sample(executable, workload)
        runs = []
        for index in range(args.runs):
            order = ["baseline", "candidate"] if args.baseline else ["candidate"]
            if index % 2:
                order.reverse()
            runs.append({name: sample(executables[name], workload) for name in order})
        records[workload] = runs
        layout = {case: set(phases) for case, phases in runs[0]["candidate"].items()}
        for run in runs:
            for samples in run.values():
                if {case: set(phases) for case, phases in samples.items()} != layout:
                    raise ValueError("Benchmark case/phase layout changed between runs")
        for case, phases in runs[0]["candidate"].items():
            for phase in phases:
                candidate = [run["candidate"][case][phase] for run in runs]
                row = {"case": case, "phase": phase,
                       "candidate_median_ms": statistics.median(candidate)}
                if args.baseline:
                    baseline = [run["baseline"][case][phase] for run in runs]
                    row["baseline_median_ms"] = statistics.median(baseline)
                    ratios = [b / a for a, b in zip(baseline, candidate) if a > 0]
                    row["median_paired_ratio"] = statistics.median(ratios) if ratios else None
                summaries.append(row)
    if args.json:
        print(json.dumps({"platform": platform.platform(),
                          "executables": {k: str(v) for k, v in executables.items()},
                          "runs": args.runs, "warmups": 1,
                          "summaries": summaries, "samples": records}, indent=2))
    else:
        for row in summaries:
            text = f'{row["case"]} {row["phase"]}: {row["candidate_median_ms"]:.3f} ms'
            if args.baseline:
                ratio = row["median_paired_ratio"]
                ratio_text = f"{ratio:.4f}" if ratio is not None else "unresolved"
                text += f' (baseline {row["baseline_median_ms"]:.3f} ms, paired ratio {ratio_text})'
            print(text)


if __name__ == "__main__":
    main()

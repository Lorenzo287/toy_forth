#!/usr/bin/env python3
import argparse
import re
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "benchmarks" / "cross-language"
PROBE = ROOT.parent
BUILD = ROOT / "build" / "cross-language"

EXPECTED = {
    "arithmetic": "10000000",
    "dispatch": "10000000",
    "fibonacci": "2178309",
    "sequence": "200000",
    "string": "200000",
    "map-lookup": "74998500000",
}
WORKLOADS = ["startup", *EXPECTED]


@dataclass(frozen=True)
class Runtime:
    name: str
    executable: Path
    source: Path | None
    arguments: tuple[str, ...] = ()
    cases: frozenset[str] = frozenset(WORKLOADS)
    cwd: Path = ROOT
    separator: bool = False
    startup_arguments: tuple[str, ...] | None = None
    internal_scale: float = 1.0

    def command(self, workload):
        if workload == "startup":
            if self.startup_arguments is not None:
                return [str(self.executable), *self.startup_arguments]
            return [str(self.executable), *self.arguments, "/dev/null"]
        if self.source is None:
            source = SOURCE / "joy" / f"{workload}.joy"
            return [str(self.executable), *self.arguments, str(source)]
        separator = ["--"] if self.separator else []
        return [
            str(self.executable),
            *self.arguments,
            str(self.source),
            *separator,
            workload,
        ]


def runtimes():
    joy_cases = frozenset(
        {"startup", "arithmetic", "dispatch", "fibonacci", "sequence", "string"}
    )
    return [
        Runtime(
            "Toy",
            BUILD / "toy-linux" / "build" / "gcc" / "release" / "toy",
            SOURCE / "toy.toy",
            ("--file",),
            separator=True,
            internal_scale=1000.0,
        ),
        Runtime(
            "Joy0",
            PROBE / "concat" / "Joy_original" / "build-linux" / "joy",
            None,
            cases=joy_cases,
            cwd=PROBE / "concat" / "Joy_original",
            internal_scale=0.001,
        ),
        Runtime(
            "Joy current",
            PROBE / "concat" / "Joy" / "build-linux" / "joy",
            None,
            ("-l",),
            joy_cases,
            PROBE / "concat" / "Joy",
            internal_scale=1.0,
        ),
        Runtime(
            "Lua",
            BUILD / "tools" / "lua-5.5.0" / "src" / "lua",
            SOURCE / "lua.lua",
            internal_scale=0.000001,
        ),
        Runtime(
            "Python",
            Path(sys.executable),
            SOURCE / "python.py",
            internal_scale=0.000001,
        ),
        Runtime(
            "Bun",
            BUILD / "tools" / "bun-linux-x64" / "bun",
            SOURCE / "javascript.js",
            ("run",),
            startup_arguments=("run", str(SOURCE / "empty.js")),
            internal_scale=0.001,
        ),
        Runtime(
            "Node",
            Path("/usr/bin/node"),
            SOURCE / "javascript.js",
            startup_arguments=(str(SOURCE / "empty.js"),),
            internal_scale=0.001,
        ),
    ]


def execute(runtime, workload):
    started = time.perf_counter_ns()
    completed = subprocess.run(
        runtime.command(workload),
        cwd=runtime.cwd,
        capture_output=True,
        text=True,
        timeout=120,
        env={"LC_ALL": "C", "PATH": "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"},
    )
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000
    if completed.returncode != 0:
        raise RuntimeError(
            f"{runtime.name} {workload} failed ({completed.returncode})\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    internal_ms = None
    expected = EXPECTED.get(workload)
    if expected is not None:
        lines = [line.strip() for line in completed.stdout.splitlines()]
        if expected not in lines:
            raise RuntimeError(
                f"{runtime.name} {workload} did not print {expected}\n"
                f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
            )
        numeric = [
            line for line in lines if re.fullmatch(r"-?\d+(?:\.\d+)?", line)
        ]
        if len(numeric) < 2:
            raise RuntimeError(
                f"{runtime.name} {workload} did not print an internal time\n"
                f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
            )
        internal_ms = float(numeric[-1]) * runtime.internal_scale
    return elapsed_ms, internal_ms


def format_ms(value):
    if value is None:
        return "—"
    if value < 10:
        return f"{value:.2f}"
    if value < 100:
        return f"{value:.1f}"
    return f"{value:.0f}"


def main():
    parser = argparse.ArgumentParser(description="Run the cross-language benchmark matrix")
    parser.add_argument("--runs", type=int, default=5, help="measured processes per cell")
    parser.add_argument("--no-warmup", action="store_true", help="skip one discarded process per cell")
    parser.add_argument("workloads", nargs="*", choices=WORKLOADS)
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")

    selected = args.workloads or WORKLOADS
    all_runtimes = runtimes()
    for runtime in all_runtimes:
        if not runtime.executable.exists():
            raise SystemExit(f"missing {runtime.name} executable: {runtime.executable}")

    process_samples = {
        workload: {runtime.name: [] for runtime in all_runtimes}
        for workload in selected
    }
    internal_samples = {
        workload: {runtime.name: [] for runtime in all_runtimes}
        for workload in selected
    }
    for workload in selected:
        available = [runtime for runtime in all_runtimes if workload in runtime.cases]
        if not args.no_warmup:
            for runtime in available:
                print(f"warmup {workload}: {runtime.name}", file=sys.stderr)
                execute(runtime, workload)
        for run in range(args.runs):
            offset = run % len(available)
            order = available[offset:] + available[:offset]
            if run % 2:
                order.reverse()
            for runtime in order:
                process_ms, internal_ms = execute(runtime, workload)
                process_samples[workload][runtime.name].append(process_ms)
                if internal_ms is not None:
                    internal_samples[workload][runtime.name].append(internal_ms)
                print(
                    f"run {run + 1}/{args.runs} {workload}: "
                    f"{runtime.name} {process_ms:.3f} ms"
                    + (f" ({internal_ms:.3f} ms internal)" if internal_ms is not None else ""),
                    file=sys.stderr,
                )

    names = [runtime.name for runtime in all_runtimes]
    print("Process wall-time medians (ms):")
    print("| Workload | " + " | ".join(names) + " |")
    print("| --- | " + " | ".join("---:" for _ in names) + " |")
    for workload in selected:
        medians = []
        for name in names:
            values = process_samples[workload][name]
            medians.append(statistics.median(values) if values else None)
        print(
            f"| {workload} | "
            + " | ".join(format_ms(value) for value in medians)
            + " |"
        )

    timed = [workload for workload in selected if workload != "startup"]
    if timed:
        print("\nIn-program CPU-time medians (ms):")
        print("| Workload | " + " | ".join(names) + " |")
        print("| --- | " + " | ".join("---:" for _ in names) + " |")
        for workload in timed:
            medians = []
            for name in names:
                values = internal_samples[workload][name]
                medians.append(statistics.median(values) if values else None)
            print(
                f"| {workload} | "
                + " | ".join(format_ms(value) for value in medians)
                + " |"
            )


if __name__ == "__main__":
    main()

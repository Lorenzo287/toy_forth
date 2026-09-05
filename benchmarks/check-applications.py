#!/usr/bin/env python3
"""Independent application oracles; correctness only, never timing evidence.

Run from any directory with --toy pointing to the interpreter under test.
--emit-checksums prints the expected values used by the Toy benchmark drivers.
Only Python's standard library is required; installed examples do not use it.
"""

import argparse
import json
import math
from pathlib import Path
import random
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
LOG_CASES = [(2000, 16), (8000, 16), (32000, 16),
             (2000, 1000), (8000, 4000), (32000, 16000)]
PARTICLE_CASES = [(250, 25, -9.81), (1000, 25, -9.81), (4000, 25, -9.81),
                  (1000, 10, 0.0), (1000, 40, 0.0), (1000, 160, 0.0)]


def quote(text):
    return json.dumps(str(text), ensure_ascii=False)


def evaluate(toy, source):
    imports = (
        f'{quote(ROOT / "examples/log-report/logs")} import '
        f'{quote(ROOT / "examples/particles/physics")} import '
        f'{quote(ROOT / "benchmarks/workloads/log-data")} import '
        '"core:json" import '
    )
    # Randomized inputs can exceed platform command-line length limits.
    with tempfile.TemporaryDirectory(prefix="toy-application-check-") as directory:
        program = Path(directory) / "check.toy"
        program.write_text(imports + source + " json.encode print", encoding="utf-8")
        result = subprocess.run(
            [str(toy), "--file", str(program)],
            capture_output=True, text=True, encoding="utf-8", check=True, timeout=60,
        )
    if result.stderr:
        raise AssertionError(result.stderr)
    return json.loads(result.stdout)


def log_text(count, routes):
    lines = []
    for i in range(count):
        if i % 251 == 0:
            lines.append("# checkpoint\n")
        status = 503 if i % 10 == 0 else 404 if i % 10 == 1 else 200
        ending = "\r\n" if i % 2 == 0 else "\n"
        lines.append(f" /item/{i * 37 % routes}?v={i % 7}\t{status}\t"
                     f"{i * 17 % 1000 + 1}\t{i * 31 % 1000 + 128} {ending}")
    return "".join(lines)


def analyze(text):
    groups = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        route, status, latency, size = line.split("\t")
        route = route.strip().partition("?")[0]
        status, latency, size = int(status), int(latency), int(size)
        group = groups.setdefault(route, [0, 0, 0, 0, 0])
        group[0] += 1
        group[1] += status >= 400
        group[2] += latency
        group[3] = max(group[3], latency)
        group[4] += size
    return groups


def log_checksum(groups):
    # Route-sensitive: swapping two groups' statistics changes this checksum.
    return sum((int(route.rsplit("/", 1)[1]) + 1) *
               (n * 3 + errors * 5 + total * 7 + maximum * 11 + size * 13)
               for route, (n, errors, total, maximum, size) in groups.items())


def seed(count):
    return [[i * 37 % 997 / 10, i * 17 % 991 / 10,
             (i * 13 % 101 - 50) / 10, (i * 29 % 103 - 51) / 10]
            for i in range(count)]


def reflect(position, velocity):
    # Deliberately use repeated reflections, not Toy's tile/floor formula.
    while position < 0 or position > 100:
        if position < 0:
            position = -position
        else:
            position = 200 - position
        velocity = -velocity
    if position == 0:
        velocity = abs(velocity)
    elif position == 100:
        velocity = -abs(velocity)
    return position, velocity


def advance(state, steps, dt, gravity):
    state = [list(particle) for particle in state]
    for _ in range(steps):
        for particle in state:
            x, y, vx, vy = particle
            vy += gravity * dt
            x, vx = reflect(x + vx * dt, vx)
            y, vy = reflect(y + vy * dt, vy)
            particle[:] = x, y, vx, vy
    return state


def summary(state):
    return [len(state), math.fsum(p[0] for p in state),
            math.fsum(p[1] for p in state),
            math.fsum((p[2] ** 2 + p[3] ** 2) / 2 for p in state)]


def check_particles(actual, expected):
    assert len(actual) == len(expected)
    for index, (left, right) in enumerate(zip(actual, expected)):
        assert len(left) == len(right) == 4
        for a, b in zip(left, right):
            assert math.isclose(a, b, rel_tol=1e-10, abs_tol=1e-8), (index, left, right)
        assert 0 <= left[0] <= 100 and 0 <= left[1] <= 100


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toy", type=Path, default=ROOT / "build/gcc/release/toy")
    parser.add_argument("--emit-checksums", action="store_true")
    args = parser.parse_args()
    toy = args.toy.resolve()
    for count, routes in LOG_CASES:
        expected = analyze(log_text(count, routes))
        if args.emit_checksums:
            print(count, routes, log_checksum(expected), "bench-log")
        else:
            actual = evaluate(toy, f"{count} {routes} logdata.make logs.analyze")
            assert actual == expected, (count, routes)
            print(f"log groups match: records={count} routes={routes}", flush=True)
    for count, steps, gravity in PARTICLE_CASES:
        expected = advance(seed(count), steps, 0.02, gravity)
        if args.emit_checksums:
            fields = " ".join(format(value, ".17g") for value in summary(expected))
            print(count, steps, gravity, "[", fields, "] bench-particles")
        else:
            actual = evaluate(toy, f"{count} particles.seed {steps} 0.02 {gravity} particles.advance")
            check_particles(actual, expected)
            print(f"particle states match: n={count} steps={steps} gravity={gravity}", flush=True)
    if args.emit_checksums:
        return

    # Independently varied records, beyond the benchmark fixture's distributions.
    randomizer = random.Random(20260905)
    text = "# independent sample\n\n" + "\n".join(
        f"{randomizer.choice(['/a', '/A', '/two words', '/café', '/literal/{}'])}?q={i}\t"
        f"{randomizer.choice([100, 200, 301, 399, 400, 404, 503, 599])}\t"
        f"{randomizer.randrange(10000)}\t{randomizer.randrange(100000)}"
        for i in range(2048)
    )
    assert evaluate(toy, f"{quote(text)} logs.analyze") == analyze(text)
    report = evaluate(toy, f"{quote(text)} logs.analyze logs.report")
    with tempfile.TemporaryDirectory(prefix="toy-log-report-check-") as directory:
        source = Path(directory) / "requests.tsv"
        source.write_text(text, encoding="utf-8")
        result = subprocess.run(
            [str(toy), str(ROOT / "examples/log-report/app"), "--", str(source)],
            capture_output=True, text=True, encoding="utf-8", check=True, timeout=60,
        )
        assert not result.stderr, result.stderr
        assert result.stdout == report  # Includes literal {} and UTF-8 in routes.
    lines = report.splitlines()
    assert lines.pop(0) == "route\trequests\terrors\tmean_ms\tmax_ms\tbytes"
    expected_groups = analyze(text)
    assert [line.split("\t")[0] for line in lines] == sorted(expected_groups)
    for line in lines:
        route, n, errors, mean, maximum, size = line.split("\t")
        expected = expected_groups[route]
        assert [int(n), int(errors), int(maximum), int(size)] == [expected[i] for i in [0, 1, 3, 4]]
        assert math.isclose(float(mean), expected[2] / expected[0], rel_tol=1e-12)

    state = [[randomizer.uniform(0, 100), randomizer.uniform(0, 100),
              randomizer.uniform(-450, 450), randomizer.uniform(-450, 450)]
             for _ in range(127)]
    actual = evaluate(toy, f"{quote(json.dumps(state))} json.decode 17 1.0 0.0 particles.advance")
    check_particles(actual, advance(state, 17, 1.0, 0.0))
    assert math.isclose(summary(actual)[3], summary(state)[3], rel_tol=1e-12)
    print("Independent text/report and multiple-bounce/energy checks passed.")


if __name__ == "__main__":
    main()

import sys
import time


def inc(value):
    return value + 1


def arithmetic():
    value = 0
    for _ in range(10_000_000):
        value += 1
    return value


def dispatch():
    value = 0
    for _ in range(10_000_000):
        value = inc(value)
    return value


def fib(value):
    if value < 2:
        return value
    return fib(value - 1) + fib(value - 2)


def sequence():
    total = 0
    for _ in range(10):
        values = []
        for _ in range(20_000):
            values.append(1)
        for value in values:
            total += value
    return total


def string_build():
    total = 0
    for _ in range(20):
        parts = []
        for _ in range(10_000):
            parts.append("x")
        total += len("".join(parts))
    return total


def map_lookup():
    values = {}
    for key in range(50_000):
        values[key] = key * 3
    total = 0
    for _ in range(20):
        for key in range(50_000):
            total += values[key]
    return total


BENCHMARKS = {
    "arithmetic": arithmetic,
    "dispatch": dispatch,
    "fibonacci": lambda: fib(32),
    "sequence": sequence,
    "string": string_build,
    "map-lookup": map_lookup,
}


if len(sys.argv) != 2 or sys.argv[1] not in BENCHMARKS:
    raise SystemExit("usage: python.py <benchmark>")

started = time.process_time_ns()
result = BENCHMARKS[sys.argv[1]]()
elapsed = time.process_time_ns() - started
print(result)
print(elapsed)

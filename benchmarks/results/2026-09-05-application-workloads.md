# Experiment: Broader application workloads

- Date: 2026-09-05
- Runtime baseline: `5a6d3a0` plus the two portability declarations recorded in
  the [native fast-path experiment](2026-09-05-native-fast-paths.md), executable
  `build/perf/baseline/toy`
- Runtime candidate: `d990a5b`, executable `build/gcc/release/toy`
- Application source: the examples and drivers accompanying this record;
  identical final Toy source runs on both interpreters
- OS / CPU: Linux 7.2.2-arch1-1, Intel Core i9-11900K, 8 cores / 16 threads
- Power state: desktop, `powersave` governor; no affinity or power changes
- Compiler / configuration: GCC 16.2.1 20260810, Nob Release, `-O3 -DNDEBUG`,
  no LTO; no builds, tests, or other benchmark runs overlapped timed comparisons
- Timing: one warmup per executable and workload, seven alternating-order
  measured pairs; a separate five-pair same-executable control
- Change under test: new application coverage and Toy source choices, **not**
  another runtime change

## Why these examples?

JSON and graph search remain useful, but are not a sufficient model of future
Toy applications. Two additional, independently runnable packages cover
different kinds of work:

| Example | Application work | Varied dimension |
| --- | --- | --- |
| [Log report](../../examples/log-report/README.md) | Split and validate text, normalize routes, aggregate a map, sort and render rows | Record count and distinct-route count |
| [Headless particles](../../examples/particles/README.md) | Repeated floating-point record transformations, branching, dynamic context, new populations | Population and step count |

The examples contain the actual implementations. Benchmark drivers import
those packages, generate deterministic fixtures, time application phases, and
check results. The applications require neither Python nor repository tools.

This is deliberately a small first set, not a claim to have discovered how
large Toy programs should be written. Synthetic TSV logs are not a real access
log corpus. Independent particles do not model interacting bodies, an ECS,
or a game loop. Input distributions and domain models are documented so that
later research can challenge them.

## Audit the Toy before judging the interpreter

The log accumulator remains on the stack through `fold`; captures hold stable
fields and a fixed-size previous group. Rendering creates fragments and joins
once. The simulation uses `map` for one output record per particle, and threads
the population through `times` without retaining a history.

These choices followed the [idiom guide](../../docs/idioms.md), but the guide
was treated as a starting hypothesis. Three targeted source comparisons used
the same candidate interpreter, 1,000 seeded particles, 100 steps, `dt=0.02`,
and gravity `-9.81`. Each pair checked that the complete summary matched;
seeding and summary calculation were outside the internal timer. The final
implementation also passed the independent full-state oracle described below.

| Source comparison | Baseline median (ms) | Candidate median (ms) | Paired ratio | Decision |
| --- | ---: | ---: | ---: | --- |
| `map` vs stack-threaded `fold`, unconditional reflection | 146.708 | 150.273 | 1.0221 | Keep the clearer `map`; no demonstrated advantage to `fold` |
| Unconditional reflection vs an in-bounds guard, both using `map` | 145.111 | 85.879 | 0.5937 | Skip unnecessary wall arithmetic |
| Guarded update vs hoisting `gravity * dt` out of the loop | 86.604 | 83.482 | 0.9478 | Compute the stable velocity change once |

Each comparison has nine alternating-order pairs. The first two are in
[particle-idioms.txt](2026-09-05-application-workloads/particle-idioms.txt);
the third, with one discarded warmup per variant, is in
[particle-hoisting.txt](2026-09-05-application-workloads/particle-hoisting.txt).
These are sequential source experiments, not independent additive speedups.
The 2.2% fold difference is too small to rank the combinators generally.

The guard is strictly `0 < position < 100`; exact walls still use the
reflection path to point velocity inward. The slow path folds an unfolded
coordinate into alternating 100-unit tiles, covering multiple wall crossings
without a bounce loop. The hoisted value is stable for the entire `advance`
call and is computed after parameter validation.

To reconstruct the alternatives, copy `physics/` to separate package
directories. For the unhoisted variant, replace `$velocity-change` in `move`
with `$gravity $dt *` and remove the `velocity-change` bindings in the entry
points. For unconditional reflection, remove the guard and use its else body
directly. For the fold variant, change only:

```toy
'step-current [ [] swap [ move push-back ] fold ] def
```

Time each imported variant with:

```toy
1000 particles.seed
monotonic-ns | start |
100 0.02 -9.81 particles.advance
monotonic-ns $start - print
particles.summary print
```

The important lesson is not that `map` is intrinsically fast: avoiding
unnecessary domain work mattered much more than changing the combinator.

## Final application timings

The following compares the earlier interpreter baseline with `d990a5b`, using
the **same final example source**, including the guard and hoisted constant.
It therefore tests whether the previously committed interpreter improvement
extends beyond the workloads used to develop it.

| Log records / routes | Baseline application (ms) | Candidate application (ms) | Paired ratio |
| --- | ---: | ---: | ---: |
| 2,000 / 16 | 4.299 | 3.116 | 0.7266 |
| 8,000 / 16 | 16.640 | 12.366 | 0.7448 |
| 32,000 / 16 | 66.644 | 48.720 | 0.7268 |
| 2,000 / 1,000 | 5.150 | 3.959 | 0.7684 |
| 8,000 / 4,000 | 20.900 | 15.710 | 0.7599 |
| 32,000 / 16,000 | 84.601 | 64.423 | 0.7614 |

Log application time is analysis plus reporting, excluding fixture creation
and validation. With 16,000 routes, the candidate medians are 51.111 ms for
analysis and 13.312 ms for reporting. With 16 routes, reporting takes only
16–22 microseconds: do not interpret its small timing differences as evidence
for an optimization.

| Particles / steps / gravity | Baseline advance (ms) | Candidate advance (ms) | Paired ratio |
| --- | ---: | ---: | ---: |
| 250 / 25 / -9.81 | 7.043 | 5.224 | 0.7413 |
| 1,000 / 25 / -9.81 | 28.161 | 20.794 | 0.7358 |
| 4,000 / 25 / -9.81 | 112.304 | 82.797 | 0.7371 |
| 1,000 / 10 / 0 | 11.084 | 8.211 | 0.7455 |
| 1,000 / 40 / 0 | 44.792 | 32.943 | 0.7342 |
| 1,000 / 160 / 0 | 179.791 | 133.376 | 0.7424 |

The earlier interpreter change reduces application time by about 23–27% in
these cases. That supports broader usefulness; it does not establish a
universal interpreter speedup.

Scaling is also encouraging: multiplying records or particle updates by 16
increases candidate application time by about 15.6–16.3 times over these
ranges. Sorting remains O(k log k) comparisons even though this range of the
log workload looks close to linear overall.

[All samples and phase medians](2026-09-05-application-workloads/paired.json)
include setup and process wall time. The latter falls from 297.331 to
222.982 ms for the six log cases and from 396.375 to 293.312 ms for the six
particle cases. Wall time includes fixture construction, loading, checks,
and output and should not replace the application-phase results.

The [same-executable control](2026-09-05-application-workloads/self-comparison.json)
has application paired ratios from 0.9624 to 1.0174. This machine/run length
does not resolve a few-percent effect reliably. The approximately quarter-time
interpreter reductions and 41% reflection reduction are substantially larger
than that variation; smaller source results warrant more caution.

## Allocation and VM-work scaling

Separate Alloc and Observe executions compared fixture creation followed by
`drop` against the same creation followed by analysis/reporting or advancement
and `drop`. Subtracting the setup-only counters removes most fixture and
loading costs. This is not an in-process counter reset: a small amount of
extra parsing/quickening overhead remains. No instrumented timing is used.

| Application | Setup-subtracted allocation calls | Requested bytes |
| --- | ---: | ---: |
| Log: 2,000 records / 16 routes | 17,760 | 1,250,181 |
| Log: 8,000 records / 16 routes | 71,444 | 5,010,417 |
| Log: 32,000 records / 16 routes | 286,174 | 20,051,073 |
| Log: 2,000 records / 1,000 routes | 23,312 | 1,867,923 |
| Log: 8,000 records / 4,000 routes | 95,342 | 7,650,765 |
| Log: 32,000 records / 16,000 routes | 383,444 | 30,824,133 |
| Particles: 1,000 / 10 steps | 67,768 | 5,347,616 |
| Particles: 1,000 / 40 steps | 270,259 | 21,309,616 |
| Particles: 1,000 / 160 steps | 1,080,716 | 85,189,585 |

The allocation study uses gravity `-9.81` for all particle cases, including
the varying-step cases; the timing driver uses zero gravity for that series.
[Raw counters and exact source commands](2026-09-05-application-workloads/traffic.json)
also contain the population-scaling cases. Counts and byte traffic are roughly
linear, not the quadratic pattern expected from repeatedly copying a growing
captured accumulator. Requested bytes are cumulative allocation traffic, not
live or peak memory, and do not include every external allocator.

The workloads differ materially in VM work. At 32,000 records / 16,000 routes,
the setup-subtracted log workload executes 4,558,752 instructions, 455,046
native continuation steps, and 278,918 program frames. At 4,000 particles /
25 steps, the simulation executes 6,504,167 instructions, 700,666 continuation
steps, and 700,347 program frames. These counts help characterize the suite;
they are not CPU profiles or proof of where the remaining time is spent.

## Correctness and portability

- Full final Release suite: 75 tests pass.
- Full final Observe suite: 76 tests pass.
- Full final leak suite: 75 tests pass. LeakSanitizer runs outside the traced
  sandbox; `HOME` is omitted to prevent REPL history writes outside the workspace.
- Two new package regressions check exact values, error handling, empty inputs,
  persistence, sorting, fractional means, gravity, exact walls, and multiple
  reflections. They run through the normal Nob harness without Python.
- Every timing process validates independent expected aggregates/summaries
  and bounds or report row counts. Checksums are compact guards, not exhaustive
  proofs of equivalence.
- `check-applications.py` independently computes all benchmark groups and
  every particle component. It also checks 2,048 independently varied text
  records, the actual file-reading CLI, Unicode and literal `{}` routes,
  sorted reports, and 127 high-velocity particles over 17 steps. Its bounce
  oracle uses repeated reflections rather than Toy's tile formula, and its
  summary uses `math.fsum`. Zero-gravity kinetic energy is checked separately.
- Both examples execute after copying only their source packages and the Toy
  executable to an isolated temporary SDK-like tree, from a different working
  directory. They do not need repository imports, Python, Nob, or core packages.
  Full `nob dist` was not run: Go is unavailable here. The existing SDK staging
  rule copies the entire `examples/` tree.
- Both new drivers run through `nob benchmark`; the phase runner was exercised
  in JSON, text, paired, and single-executable modes.

## Reproduce and extend

```console
./nob build
./nob benchmark log-report particles --runs 7
python3 benchmarks/check-applications.py
python3 benchmarks/measure-applications.py \
  --baseline build/perf/baseline/toy --toy build/gcc/release/toy --runs 7 --json
python3 benchmarks/measure-applications.py \
  --baseline build/gcc/release/toy --runs 5 --json
./nob --mode alloc build
./nob --mode observe build
```

Preserve baseline executables and their core packages using the instructions in
the [preceding experiment](2026-09-05-native-fast-paths.md). Binary SHA-256:

```text
baseline  3657c69ff4e16c76b83cc1094dfbaf5b2b44e82ee32c97d6bdaeb5982ac648f0
candidate 43f98ec9b66135bbba29edff2a0e2e063ef0fa42c745183a87d1ef1cd0b6777a
```

For allocation/Observe reproduction, feed each `setup_source` and `full_source`
from `traffic.json` to the corresponding interpreter's `--eval` option,
replacing the absolute checkout prefix as necessary. Observe also needs
`--metrics-json <output-path>`. Subtract additive counters, not high-water
marks; preserve the complete reports when investigating individual allocation
sites.

Useful next coverage is a real, skewed text corpus; JSON shapes beyond one
homogeneous record family; graph topologies beyond narrow synthetic shapes;
and a host-driven event processor. This first pair deliberately does not cover
streaming I/O, host callbacks, deep recursion, or large dynamic scope chains.
No packed numeric representation, bytecode architecture, or new builtin is
required to obtain the examples' present scaling. Those remain separate
experiments to motivate with evidence from concrete applications.

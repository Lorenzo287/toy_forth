# Experiment: Native execution fast paths

- Date: 2026-09-05
- Baseline: `5a6d3a0`, plus the two `_XOPEN_SOURCE` portability declarations
  described below; executable `build/perf/baseline/toy`
- Candidate: baseline plus this working tree's runtime changes;
  executable `build/perf/capture-equality/toy`
- OS / CPU: Linux 7.2.2-arch1-1, Intel Core i9-11900K, 8 cores / 16 threads
- Power state: desktop, `powersave` governor; affinity and power settings left
  unchanged; no builds or other benchmark runs overlapped measured comparisons
- Primary compiler: GCC 16.2.1 20260810, Nob Release, `-O3 -DNDEBUG`, no LTO
- Cross-check: GCC 15.3.0, `-O3 -DNDEBUG`, no LTO, independently built baseline
  and candidate
- Timing: fresh-process wall time, alternating baseline/candidate order, one
  warmup per binary and workload; nine pairs for the full suite, seven for the
  GCC 15 and checksum-validated scalar comparisons

## Question and result

Can reducing overhead shared by native words improve the whole interpreter
without changing the execution representation or specializing another combinator?

Yes. The main opportunity was the boundary between C translation units. The
runtime's stack operations and argument checks were small, but their ordinary
external-function interfaces hid the implementation from callers. A native word
would validate its inputs, call wrappers to pop them, and call another wrapper
to push its result. Without LTO, these boundaries prevent the compiler from
combining the checks and stack accesses.

Making those fast paths visible produces broad improvements. JSON, graph search,
and recursive/control workloads improve substantially; collection workloads
also benefit. The construction-patterns result is effectively neutral. No
workload has a slower paired median in the full sweep, though this is not a
claim that all programs or platforms will improve.

## Changes

1. Inline internal vector push/pop and data-stack access. A push checks capacity
   locally; storage growth remains out of line. Ownership transfer, underflow
   behavior, retained capacity, and Observe stack high-water reporting remain
   unchanged. Immediate integer construction is also visible to callers; boxed
   allocation remains a function.
2. Inline successful native argument checks and callable classification. The
   compiler can share loads between validation and the following operation.
   Stack/type error formatting stays in two ordinary functions with the same
   diagnostic text. This does not remove runtime checks.
3. Combine quickened call resolution and dispatch under one generation check.
   Cache native function targets directly in the existing sidecar entry instead
   of loading a dictionary entry and checking its word type on every hit. User
   calls retain dense indexes, which survive dictionary array relocation. Native
   code stays loaded for the context's lifetime; redefinition and generation
   wrap invalidate both forms.
4. Compare capture names for equality using length and bytes. Capture resolution
   previously called the lexicographic string-ordering routine, including byte
   comparisons when lengths already ruled out equality. Dynamic scope still
   searches the same frames in the same order; its asymptotic complexity is
   unchanged.

No bytecode stream, public ABI change, frame-layout expansion, or synchronous VM
re-entry is introduced. The cache entry uses a union, so it remains 24 bytes on
the measured 64-bit platform. Object and frame layouts are unchanged.

## Full GCC 16 comparison

Percentages below are reductions in elapsed time, calculated from the median
paired ratio rather than the ratio of independent medians.

| Workload | Baseline median (ms) | Candidate median (ms) | Paired ratio | Less time |
| --- | ---: | ---: | ---: | ---: |
| construction-patterns | 214.490 | 211.596 | 0.9877 | 1.23% |
| control-ownership | 13.010 | 9.935 | 0.7833 | 21.67% |
| deque | 205.048 | 188.181 | 0.9178 | 8.22% |
| dispatch | 1711.056 | 1545.634 | 0.9041 | 9.59% |
| graph-search | 77.906 | 56.271 | 0.7236 | 27.64% |
| json | 554.868 | 417.238 | 0.7519 | 24.81% |
| list | 66.528 | 56.425 | 0.8501 | 14.99% |
| map | 58.156 | 49.592 | 0.8580 | 14.20% |
| pqueue | 146.927 | 139.344 | 0.9482 | 5.18% |
| runtime-internals | 482.158 | 336.650 | 0.6956 | 30.44% |
| sequence-algorithms | 18.183 | 16.838 | 0.9290 | 7.10% |
| set | 182.789 | 172.207 | 0.9449 | 5.51% |
| string | 71.162 | 57.557 | 0.8100 | 19.00% |
| vector | 72.747 | 49.781 | 0.6865 | 31.35% |

[All pairs](2026-09-05-native-fast-paths/gcc16.txt) include the executable paths
and runner metadata. A preliminary three-pair baseline-versus-itself comparison
measured ratios of 1.0084 for JSON, 1.0015 for runtime-internals, and 0.9996 for
graph search; [raw control results](2026-09-05-native-fast-paths/self-comparison.txt).
Short processes and effects near 1% warrant particular caution.

## Compiler and workload cross-checks

| Workload | GCC 15 baseline (ms) | GCC 15 candidate (ms) | Paired ratio | Less time |
| --- | ---: | ---: | ---: | ---: |
| dispatch | 1666.240 | 1539.435 | 0.9177 | 8.23% |
| json | 550.005 | 411.406 | 0.7528 | 24.72% |
| runtime-internals | 502.955 | 333.858 | 0.6633 | 33.67% |
| graph-search | 78.081 | 56.612 | 0.7182 | 28.18% |
| vector | 75.169 | 49.182 | 0.6524 | 34.76% |
| string | 70.744 | 57.834 | 0.8181 | 18.19% |

[GCC 15 pairs](2026-09-05-native-fast-paths/gcc15.txt) support the same direction
and scale of improvement. This checks another compiler version on the same CPU,
not another compiler family or architecture. Clang and Windows were not tested.

The existing `cross-language/toy.toy` workload was also run against the GCC 16
baseline and candidate. Every measured process returned the expected checksum;
the comparison used wall time including startup, not the script's CPU timer.

| Case | Baseline (ms) | Candidate (ms) | Paired ratio |
| --- | ---: | ---: | ---: |
| arithmetic | 115.389 | 100.276 | 0.8678 |
| user-word dispatch | 237.324 | 215.732 | 0.9129 |
| recursive Fibonacci(32) | 780.995 | 604.366 | 0.7727 |
| sequence | 8.617 | 6.480 | 0.7574 |
| string | 5.344 | 3.852 | 0.7208 |
| map lookup | 94.548 | 62.576 | 0.6618 |

[Scalar pairs and checksums](2026-09-05-native-fast-paths/scalar.txt). The two
shortest cases are especially sensitive to process startup and scheduling.

## Attribution

An exploratory GCC `-O3 -g -pg` baseline build, run on JSON and inspected with
`gprof`, reported 26,576,059 vector pushes, 21,732,276 vector pops, 17,194,123
stack-pop wrapper calls, and 25,810,451 string-order comparisons. About 24.6
million of those comparisons were attributed to VM execution. Instrumentation
and compiler inlining affect attribution; these counts motivated experiments,
and the instrumented elapsed time was not used as performance evidence.

The five-pair incremental experiments were:

| Increment | JSON ratio | Dispatch ratio | Runtime ratio | Graph ratio |
| --- | ---: | ---: | ---: | ---: |
| Inline stack/vector access | 0.8783 | 1.0297 | 0.7740 | 0.8098 |
| Inline integer construction | 1.0084 | 1.0011 | 1.0029 | — |
| Direct native targets / one cache check | 0.9817 | 0.9651 | 0.9930 | 0.9828 |
| Inline successful validation | 0.9357 | 0.9225 | 0.9368 | 0.9332 |
| Capture name equality | 0.9148 | 0.9907 | 0.9879 | 0.9545 |

[Raw incremental pairs](2026-09-05-native-fast-paths/steps.txt). Each row compares
against the preceding increment, so these are not independent ablations or
additive percentages. Integer construction alone was neutral in these cases;
no individual speedup is claimed for it. The initial dispatch regression was
resolved by the subsequent call and validation changes.

Baseline and candidate Observe JSON output are identical, including 32,348,208
instructions, 11,861,779 call instructions, 5,213,947 program frames, 6,195,151
native continuation steps, 223 dictionary lookups, and 74 quick-program cache
misses. [Shared metrics](2026-09-05-native-fast-paths/json-metrics.json) record all
counters. These changes reduce the cost of the same VM work rather than reduce
the number of Toy instructions or frames.

## Validation and tradeoffs

- Complete GCC 16 Release suite: 73 tests pass.
- Complete GCC 16 Observe suite: 74 tests pass.
- Complete GCC 16 leak suite: 73 tests pass.
- Added a C regression covering one quotation shared across two contexts with
  different native definitions, warmed native replacement, native-to-user
  replacement, dictionary growth around a cached user target, and replacement
  from an active native call while the generation counter wraps.
- Existing diagnostics, debugger, captures, continuations, collection ownership,
  embedding, packages, and extension tests pass.
- The leak suite ran outside the traced sandbox because LeakSanitizer cannot
  inspect processes under ptrace. Test children omitted `HOME` to disable REPL
  history writes outside the workspace; otherwise the unmodified baseline's
  interactive REPL test failed on the sandbox's unwritable history path.

The executable `.text` section grows from 264,083 to 316,915 bytes, about 20.0%.
This is the main tradeoff: more code is exposed to optimization at each call
site, which can cost instruction-cache space on other machines. The full
workload sweep and second GCC version justify keeping this change here, but
do not justify indiscriminate further inlining.

Two prerequisite portability fixes define `_XOPEN_SOURCE=700` before headers in
the package loader and REPL so strict-C11 glibc builds declare `realpath`.
Both measured baselines include those fixes. Unix Nob bootstrap artifacts are
also ignored. None of these changes is credited with a performance gain.

## Reproduction and next questions

```console
cc nob.c -o nob
./nob build
./nob benchmark --compare build/perf/baseline/toy build/perf/capture-equality/toy --runs 9
./nob benchmark dispatch json runtime-internals graph-search vector string --compare build/perf/gcc15-baseline/toy build/perf/gcc15-candidate/toy --runs 7
```

Preserve each executable after building its corresponding source revision. Each
saved binary needs a sibling `core` directory; these runs used symlinks to
`build/gcc/release/core`. The baseline source snapshot was made with
`git archive 5a6d3a0`, then given only the two portability declarations. For the
GCC 15 cross-check, run this command separately in each source tree:

```console
gcc-15 -std=c11 -O3 -DNDEBUG -D_POSIX_C_SOURCE=200809L -Iinclude -Isrc -Ideps/linenoise src/*.c src/generated/tf_docs.c src/cli/*.c deps/linenoise/linenoise.c -lm -ldl -o /path/to/saved/toy
```

Scalar samples invoke each saved binary with
`--file benchmarks/cross-language/toy.toy -- <case>`. Measure the subprocess with
`time.perf_counter_ns`, require status zero and exactly two output lines, and
verify the first line against `benchmarks/cross-language/run.py`'s checksum
table. Discard one warmup per binary, alternate order for seven pairs, and take
the median candidate/baseline wall-time ratio.

Saved executable SHA-256 digests:

```text
GCC 16 baseline   3657c69ff4e16c76b83cc1094dfbaf5b2b44e82ee32c97d6bdaeb5982ac648f0
GCC 16 candidate  43f98ec9b66135bbba29edff2a0e2e063ef0fa42c745183a87d1ef1cd0b6777a
GCC 15 baseline   7a23422e0e41a9405c8c6c1a4a21cc8505f182f526ff831caf399b92ee632e02
GCC 15 candidate  d0f5f4a0827f3c0fcb4b055b50f17c3e1cf12784a4581d722aaafd13cba7b6ca
```

The next useful questions concern remaining continuation scheduling, dynamic
capture traversal, and temporary boolean allocation. LTO is also worth a
separate experiment: it may expose some of the same optimization opportunities
automatically, but was not enabled or measured here. A replacement execution
representation remains a distinct architectural experiment; these results do
not establish that bytecode is necessary or sufficient for another gain.

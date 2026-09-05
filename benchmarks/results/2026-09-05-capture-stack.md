# Experiment: Shared dynamic capture stack

- Date: 2026-09-05
- Baseline: `d990a5b`, `build/perf/runtime-stage2/baseline/toy`
- Candidate: baseline plus the runtime changes accompanying this record,
  `build/perf/runtime-stage2/flat/toy`
- Workloads: identical source on both runtimes, including the newly added log
  and particle applications and a controlled capture benchmark
- OS / CPU: Linux 7.2.2-arch1-1, Intel Core i9-11900K, 8 cores / 16 threads
- Power state: desktop, `powersave` governor; no affinity or power changes
- Primary build: GCC 16.2.1 20260810, Nob Release, `-O3 -DNDEBUG`, no LTO
- Cross-check: GCC 15.3.0, same optimization level, no LTO
- Sampling: one warmup, nine alternating-order pairs for the full 17-workload
  GCC 16 sweep and application phases; seven pairs for eight GCC 15 workloads;
  five pairs for same-executable controls
- No builds, tests, or other benchmark runs overlapped timed comparisons

## Result

Moving capture bindings out of individual execution frames yields a shared
runtime improvement without changing Toy syntax, scope semantics, public ABI,
or the execution representation. The full GCC 16 sweep reduces JSON wall time
by 12.0%, dispatch by 11.1%, graph search by 5.5%, and particle workload wall
time by 14.1%. Several collection workloads improve modestly; two workloads
have paired medians less than 0.4% slower, which is not a meaningful regression
against the observed control variation.

The new application-phase measurements independently support the result:
log analysis/reporting improves by approximately 9–19% across its six cases,
and particle advancement by approximately 13–16%. The example source is
unchanged in this stage. These gains are relative to the **already optimized**
`d990a5b`, not the earlier pre-fast-path baseline.

## Why this architecture?

Previously, every program frame embedded two capture slots and allocated a
separate array when that was insufficient. Lookup walked the entire frame
stack, skipping native frames and searching each program frame's bindings.
Completion released the bindings and freed that frame's capture array.

The new representation has one context-owned vector of active bindings.
Frames retain only their segment's base index and length. The executing frame
appends to the current suffix or replaces a binding within its own segment.
Lookup scans the binding vector backwards. On return or unwind, the frame
releases its segment and restores the vector's length.

This changes three related costs:

1. Capture-free frames contribute no lookup work, and bindings have contiguous
   storage rather than separate arrays reached through frames.
2. Calls reuse binding capacity instead of repeatedly allocating and freeing
   per-frame arrays. Capture-free completion also loses the old `free(NULL)`
   path. Names and values retain the same references as before.
3. Every execution frame becomes smaller: **120 to 88 bytes** on this platform.
   Native frames share the frame union, so they benefit too. Program-frame
   payload falls from 88 to 56 bytes; context size grows by 24 bytes.

These mechanisms were changed together; the experiment does not assign a
separate timing percentage to each. Executable `.text` remains essentially
unchanged, falling from 316,915 to 316,851 bytes.

Indices, not pointers into either growable array, preserve frame/capture
independence during relocation. Rebinding never overwrites an outer scope;
reverse lookup retains nearest-binding semantics. The debugger uses the same
segments. Per-frame destruction order is preserved, including resource
destructors. The iterative VM and native continuation protocol are unchanged.

The tradeoff is retained high-water capacity in long-lived contexts: binding
storage is freed at context teardown, not on each return. Completed scopes
retain no object references. A transiently wide/deep set of captures can leave
more idle binding storage than before; smaller frame storage offsets some,
but not necessarily all, of that difference. Lookup is still linear in active
bindings, not constant time.

## Profiling motivation

An exploratory `-O3 -g -pg` baseline recorded 10,351,317 capture lookups and
5,213,947 program-frame pushes for JSON; 3,373,542 lookups and 1,018,345 pushes
for the log driver; and 7,983,729 lookups and 2,423,163 pushes for particles.
Application driver profiles include fixture construction and checks.

The [JSON](2026-09-05-capture-stack/json-profile.txt),
[log](2026-09-05-capture-stack/log-profile.txt), and
[particle](2026-09-05-capture-stack/particles-profile.txt) profiles are motivation,
not timing evidence. These are short, instrumented runs with coarse sampling;
inlining and instrumentation affect attribution, and zero sampled self time
does not imply zero cost. Release comparisons and allocation counters decide
whether the architectural change pays off.

## Full GCC 16 comparison

Times below are fresh-process wall medians; ratios are medians of matching
candidate/baseline pairs, not ratios of independent medians.

| Workload | Baseline (ms) | Candidate (ms) | Paired ratio |
| --- | ---: | ---: | ---: |
| captures | 44.563 | 29.738 | 0.6696 |
| construction-patterns | 222.688 | 217.638 | 0.9857 |
| control-ownership | 9.090 | 9.197 | 1.0014 |
| deque | 193.238 | 190.888 | 0.9759 |
| dispatch | 1590.859 | 1421.568 | 0.8891 |
| graph-search | 59.658 | 56.284 | 0.9450 |
| json | 425.498 | 371.440 | 0.8799 |
| list | 58.211 | 57.642 | 0.9852 |
| log-report | 234.346 | 220.108 | 0.9287 |
| map | 50.517 | 48.247 | 0.9354 |
| particles | 303.648 | 260.087 | 0.8591 |
| pqueue | 144.395 | 139.606 | 0.9752 |
| runtime-internals | 341.120 | 323.026 | 0.9455 |
| sequence-algorithms | 17.386 | 17.444 | 1.0034 |
| set | 175.278 | 172.263 | 0.9782 |
| string | 60.381 | 57.496 | 0.9631 |
| vector | 50.440 | 47.212 | 0.9237 |

[All GCC 16 pairs](2026-09-05-capture-stack/gcc16.txt). An initial five-pair
screen showed construction-patterns 2.7% slower; that did not persist in the
longer sweep or second compiler. The final same-executable controls have ratios
of 1.0050 for construction-patterns, 1.0234 for log-report, and 1.0130 for
particles; [control pairs](2026-09-05-capture-stack/self-comparison.txt).
Treat small changes of this order as unresolved rather than ranking them.

GCC 15 repeats the main direction: JSON 0.9130, dispatch 0.9276, graph search
0.9470, runtime-internals 0.9568, log-report 0.9217, particles 0.8637, and the
capture workload 0.6727. Construction-patterns is neutral at 0.9936.
[GCC 15 pairs](2026-09-05-capture-stack/gcc15.txt). This is a second compiler
version on one CPU, not evidence across compiler families or architectures.

## Application phases and controlled scope cases

The phase runner excludes fixture generation, checks, loading, and output.
For logs, application time includes analysis and report generation; for
particles, it includes advancement only. The largest cases are:

| Application case | Baseline (ms) | Candidate (ms) | Paired ratio |
| --- | ---: | ---: | ---: |
| 32,000 log records / 16 routes | 52.561 | 44.704 | 0.8519 |
| 32,000 log records / 16,000 routes | 68.094 | 61.810 | 0.9105 |
| 4,000 particles / 25 steps / gravity -9.81 | 85.688 | 74.274 | 0.8603 |
| 1,000 particles / 160 steps / gravity 0 | 137.088 | 117.582 | 0.8539 |

[All application-phase samples](2026-09-05-capture-stack/applications.json).
Keep reporting separate from analysis: sorting and formatting have different
costs, and a report of only 16 rows is too short to resolve small differences.

The new `captures.toy` is a deliberately controlled stress test, not another
representative application. It performs 20,000 reads through chains of calls
that bind no captures, then tests repeated calls with eight local bindings:

| Controlled case | Baseline (ms) | Candidate (ms) |
| --- | ---: | ---: |
| Capture-free chain, depth 0 | 0.415 | 0.274 |
| Capture-free chain, depth 32 | 1.275 | 0.274 |
| Capture-free chain, depth 256 | 8.927 | 0.328 |
| Eight locals, 20,000 calls | 6.414 | 5.340 |
| Eight locals, 80,000 calls | 25.005 | 21.217 |

[Nine paired internal-timer samples](2026-09-05-capture-stack/capture-phases.json).
The small candidate increase at depth 256 includes constructing and unwinding
the chain. Removing capture-free depth from lookup is an asymptotic change;
it does not remove the cost of searching many actual bindings. The much larger
deep-chain gain should not be advertised as an application-wide speedup.

## Allocation and execution evidence

The application study compares setup-only and setup-plus-application processes,
then subtracts additive counters, following the
[application workload record](2026-09-05-application-workloads.md). This removes
most fixture/loading costs but is not an in-process counter reset. The same
Toy source and inputs were reused for the current Alloc/Observe executions.

| Case | Baseline allocation calls | Candidate calls | Baseline requested bytes | Candidate bytes |
| --- | ---: | ---: | ---: | ---: |
| Log: 32,000 records / 16 routes | 286,174 | 158,174 | 20,051,073 | 7,762,561 |
| Log: 32,000 records / 16,000 routes | 383,444 | 255,444 | 30,824,133 | 18,535,621 |
| Particles: 4,000 / 25 steps | 694,124 | 494,016 | 54,750,624 | 35,543,200 |
| Particles: 1,000 / 160 steps | 1,080,716 | 760,038 | 85,189,585 | 54,425,681 |

All allocation-study particle cases use gravity -9.81. Calls fall by about
33–45% across the log cases and 29–33% across the particle cases. In the log
workload, the reduction is four checked allocation calls per record, plus
small startup differences: the parser and aggregator no longer repeatedly
grow their private capture arrays. Counts and byte traffic remain roughly
linear. Requested bytes are cumulative allocator traffic, not live/peak memory.

Observe execution counts match the baseline exactly for both setup and full
runs of all twelve application cases: instructions, calls, continuation steps,
program/native frames, and stack high-water counts are unchanged. The JSON
driver's [execution counts](2026-09-05-capture-stack/json-metrics.json) also match
its recorded baseline. This is cheaper
execution of the same VM work, not work removed from the benchmark.
[Current counters and baseline deltas](2026-09-05-capture-stack/traffic.json).
Cache hit/miss counts are not required to match: allocation layout can change
the addresses used to choose cache sets.

## Validation

- Full Release suite: 77 tests pass.
- Full Observe suite: 78 tests pass.
- Full leak suite: 77 tests pass in an untraced run. `HOME` was omitted to
  avoid REPL history writes outside the workspace.
- The new C capture-storage test passes AddressSanitizer and UBSan with
  assertions enabled. Leak detection was disabled in that sandboxed ASan run;
  the separate full LeakSanitizer suite supplies the leak check.
- The independent log/particle full-state checker passes against both GCC 16
  and GCC 15 candidates, as do the checks embedded in every measured driver.
- New Toy regressions cover deep/wide scopes, rebinding, shadowing, repeated
  quotations, partial binding errors, predicate/`infra` isolation, and persistent
  collection values.
- New C regressions cover frame/binding-array growth, per-frame debugger
  inspection, nested execution in an independent context, normal completion,
  debug abort, native interruption, errors, exit, context reuse, and resource
  destruction order. Capture references disappear after every unwind path.
- Generated builtin outputs remain current. No metadata, public ABI, or
  installed example changes were required for this optimization.

## Reproduction

```console
./nob build
./nob benchmark --compare build/perf/runtime-stage2/baseline/toy build/perf/runtime-stage2/flat/toy --runs 9
python3 benchmarks/measure-applications.py --baseline build/perf/runtime-stage2/baseline/toy --toy build/perf/runtime-stage2/flat/toy --runs 9 --json
./nob benchmark json graph-search runtime-internals dispatch construction-patterns log-report particles captures --compare build/perf/gcc15-candidate/toy build/perf/runtime-stage2/gcc15-flat/toy --runs 7
python3 benchmarks/check-applications.py
env -u HOME ./nob test
env -u HOME ./nob --mode observe test
env -u HOME ./nob --mode leak test
```

Save baseline and candidate executables before rebuilding; each saved binary
needs its matching core packages beside it. The GCC 15 baseline is the candidate
from the [native fast-path experiment](2026-09-05-native-fast-paths.md). Its
manual compile command was repeated for this candidate with identical flags.

For the controlled capture phases, run `benchmarks/captures.toy`, parse each
line's `elapsed_ns`, discard one warmup, alternate binary order for nine pairs,
and take each case's median and median paired ratio. Every process checks the
closed-form expected sums before printing its timings.

Allocation reproduction uses `setup_source` and `full_source` from the
preceding application's `traffic.json`; replace its checkout prefix as needed.
Use Alloc stderr totals and Observe `--metrics-json`, and subtract additive
counters only. Gprof motivation profiles used the preceding experiment's
manual GCC command with `-O3 -g -pg -DNDEBUG`, then `gprof -b -p` on each driver.

Saved binary SHA-256 digests:

```text
GCC 16 baseline  43f98ec9b66135bbba29edff2a0e2e063ef0fa42c745183a87d1ef1cd0b6777a
GCC 16 candidate 533d5576f35fb1413e4970da9dd732c5b39374dc1047e8e51118086786927bbc
GCC 15 baseline  d0f5f4a0827f3c0fcb4b055b50f17c3e1cf12784a4581d722aaafd13cba7b6ca
GCC 15 candidate 50ef80675bd100cbe51a1514f39cb64a9838314620809460eb84c85bd2370635
```

## Next candidates, not claimed improvements

1. **Continuation setup.** Boolean `if`/`ifelse` still enter the general
   predicate/continuation controller even when the condition is already known.
   More generally, quotation dispatch repeatedly acquires sidecars and creates
   frames. Test eliminating unnecessary controllers or reusing execution state
   while preserving callable predicates, stack isolation, debugging, and errors.
2. **Temporary scalar values.** Comparisons, boolean composition, and floating
   arithmetic create transient objects. Pooling already hides some allocation
   calls, so measure construction/refcount traffic, not just allocator totals.
   Shared or immediate booleans need a source-span and ownership audit; float
   representation changes have a larger portability/API implementation cost.
3. **Capture-name indexing.** Many active bindings still require a linear
   name search. Interning or a context-local binding index could help, but
   must justify its maintenance cost under frequent binding, shadowing, and
   unwind. The present flat stack is a useful baseline before adding that
   complexity.

Keep all application phases and idiom controls in those comparisons. LTO and a
bytecode prototype remain separate experiments: neither is needed to obtain
this milestone, and this result does not predict their benefit.

# Benchmarks

This directory contains performance experiments. Keep
workloads deterministic and focused enough that a result can be tied to one
implementation choice.

Nob builds an optimized interpreter and runs every workload by default. The
commands below assume the bootstrapped Nob executable is available as `nob`;
use its actual path when it is not on `PATH`.

```console
nob benchmark
```

Select workloads, change the sample count, choose another build mode, or use an
existing executable:

```console
nob benchmark vector --runs 10
nob --cc clang --mode profile benchmark dispatch
nob benchmark dispatch --toy path/to/toy
```

## Comparing Two Builds

Use paired comparison for optimization experiments instead of running all
baseline samples and then all candidate samples:

```console
nob benchmark dispatch runtime-internals \
  --compare path/to/baseline/toy path/to/candidate/toy
```

Comparison mode runs one unmeasured warmup per executable and workload, then
15 measured pairs by default. Odd pairs run the baseline first; even pairs run
the candidate first. It reports both wall-time medians and the median of
`candidate / baseline` for matching pairs. The paired ratio is the primary
result because it is less sensitive to gradual changes in temperature, CPU
frequency, and background activity.

Use `--runs` to change the number of pairs and `--warmup` when a workload needs
more preparation:

```console
nob benchmark dispatch \
  --compare path/to/baseline/toy path/to/candidate/toy \
  --runs 21 --warmup 2
```

With no workload names, comparison mode runs every Toy benchmark. It does not
run C benchmarks: those are compiled artifacts rather than source workloads
that can be passed to two Toy executables. Workload output is hidden during a
comparison so that the paired record stays readable, but a nonzero process
exit still fails the run.

The comparison header records executable paths, platform, logical processor
count, pair count, and warmups. Save the complete output alongside a durable
result note. For example, in PowerShell:

```powershell
.\nob.exe benchmark dispatch --compare old\toy.exe new\toy.exe 2>&1 |
    Tee-Object build\dispatch-comparison.txt
```

The runner cannot infer which commits and compiler flags produced arbitrary
executables, the CPU model, or the active power plan. Record those separately
when promoting a result to `benchmarks/results/`.

It also deliberately does not change process priority, CPU affinity, or the
host power policy. Those controls are platform-specific and can make a result
less representative; both children simply inherit the same environment. When
in doubt, compare an executable with itself. A ratio that does not settle near
1.0 means the workload is too short or the machine is too variable for the
size of effect being investigated.

The Toy scripts use `monotonic-ns` and print integer nanosecond durations for
individual operations. C workloads are compiled against the matching runtime
and report their own operation timings in the same way. The runner also reports
wall time for each fresh process and its median. A custom `--toy` executable can
run only Toy-script workloads. Compare results only across the same machine,
compiler, build configuration, and workload.
Use the workflow in
[`docs/development/observability.md`](../docs/development/observability.md)
when a change needs runtime counters, allocation statistics, or a sampled
profile in addition to Release timing.
Before drawing a conclusion:

1. Plug into AC power, choose one power mode, close avoidable background work,
   and let the machine settle before warming both builds.
2. Record the commit, compiler/version, build type, OS, CPU, power state, and
   command.
3. Prefer 15 or more alternating pairs. Make important workloads long enough
   to dominate startup and timer resolution; do not select the fastest run.
4. Change one implementation technique at a time. Include a control workload
   that should not use the changed path, and repeat small results in a separate
   session.
5. Confirm behavior and leak tests separately; a benchmark is not a regression
   test.
6. Store durable measurements from meaningful experiments under `results/`
   using the provided template.

Current workloads:

- `cross-language/`: optional checksum-validated comparison with Joy0, current
  Joy, Lua, Python, Bun, and Node, run separately under Linux;
- `construction-patterns.toy`: equivalent captured, stack-threaded, list,
  `infra`, and one-shot conversion patterns for vectors, maps, and strings;
- `control-ownership.toy`: ambient collection updates through state-threading
  control combinators, guarding against accidental snapshot retention;
- `deferred.c`: public C API queueing and VM draining for a burst of deferred
  callback arguments;
- `dispatch.toy`: inline native calls versus user-word dispatch.
- `deque.toy`: unique/shared endpoint updates, pops, wraparound, and projection.
- `graph-search.toy`: BFS and Dijkstra scaling for the standalone graph
  examples under `examples/graphs/`.
- `list.toy`: constant-time front operations, linear traversal, and
  copy-left/share-right concatenation.
- `json.toy`: decode and encode scaling for the Toy-written `core:json`
  package.
- `map.toy`: unique growth and replacement, shared updates, lookup, and
  absent-key deletion.
- `set.toy`: unique growth, duplicate insertion, shared updates, membership,
  present/absent removal, algebra, and relation predicates.
- `pqueue.toy`: unique/shared heap updates, non-consuming peek, pop, and ordered
  pair projection.
- `runtime-internals.toy`: native continuations, dynamic captures, predicate
  stack snapshots, and recursion-scheme scheduling.
- `sequence-algorithms.toy`: sort and unique crossover workloads by size,
  shape, and sequence family.
- `string.toy`: short-string storage, byte extraction and traversal, flat
  string transforms, splitting, and incremental growth.
- `vector.toy`: unique `push-back`, non-shrinking `pop-back`, indexed reads,
  and unique/shared-left `concat`.

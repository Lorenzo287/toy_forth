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

1. Record the commit, compiler/version, build type, OS, CPU, and command.
2. Run enough samples to see normal variance; do not select the fastest run.
3. Change one implementation technique at a time.
4. Confirm behavior and leak tests separately; a benchmark is not a regression
   test.
5. Store durable measurements from meaningful experiments under `results/`
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

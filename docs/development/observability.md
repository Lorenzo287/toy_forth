# Runtime Observability

Toy keeps performance evidence separate from normal Release execution. Use
each mode for the question it can answer:

| Question | Build mode | Evidence |
| --- | --- | --- |
| Did elapsed time change? | `release` | Repeated benchmark medians |
| Did VM behavior change? | `observe` | Deterministic JSON counters |
| Did checked allocation traffic change? | `alloc` | Calls, bytes, and ranked source sites |
| Where is CPU time spent? | `profile` | Sampled stacks with symbols |
| Is ownership correct? | `leak` | Leak-instrumented regressions |

`observe` defines `TF_OBSERVE`, while `alloc` defines `TF_ALLOC_STATS`.
Normal Release builds contain neither the metrics fields and increments nor
the allocation counters. Profile builds change compiler flags but do not add
runtime counters.

## Recommended Workflow

Measure timing with Release first:

```console
nob benchmark json --runs 7
```

When comparing a change against a saved baseline executable, use the paired
runner so both builds experience nearly the same machine state:

```console
nob benchmark json \
  --compare build/baseline/toy build/gcc/release/toy \
  --runs 15 --warmup 1
```

It warms both builds, alternates their order in each pair, and reports the
median candidate/baseline ratio. Treat that paired ratio as the timing result;
the two independent medians are useful context but do not preserve pairing.
See [`benchmarks/README.md`](../../benchmarks/README.md) for laptop setup,
output capture, control workloads, and the limits of recorded environment
metadata.

If the result is meaningful, run the same workload with deterministic runtime
metrics:

```console
nob --mode observe build
build/gcc/observe/toy --metrics-json metrics.json --file benchmarks/json.toy
```

The versioned JSON reports cumulative execution, frame, stack high-water,
dictionary-cache, quick-program-cache, and dispatch counts for one context.
These counters explain changes in runtime work, but their increments perturb
timing. Do not use an Observe build for elapsed-time conclusions.

Use AllocationStats when allocation traffic may explain the result:

```console
nob --mode alloc benchmark json --runs 1
```

The report counts calls through Toy's checked allocation helpers, sums the
requested bytes, and ranks call sites by byte traffic. Its fixed internal site
table does not allocate. It does not report live or peak memory, allocator
overhead, or allocations performed directly by external libraries.

When counters identify a hot workload but not a hot function, capture sampled
stacks from an optimized Profile build:

```console
nob --cc clang --mode profile build
samply record build/clang/profile/toy --file benchmarks/json.toy
```

Profile builds retain symbols and frame pointers. On Windows, use Clang or
Clang-CL because Samply does not support MinGW GCC profiles. Samply keeps the
recording local until its Firefox Profiler interface is explicitly asked to
publish it. Sampling is statistical, so confirm proposed changes with Release
benchmarks rather than sample percentages alone.

Finally, validate behavior and ownership separately:

```console
nob test
nob --mode leak test
```

## Interpreting Results

- Compare the same workload, compiler, machine, and power state.
- Use the paired runner to alternate baseline and candidate process order;
  never select the fastest run.
- Treat counters as attribution, not timing. A faster run with identical
  counters may be a code-generation or memory-layout effect.
- Rebuild or change code placement before acting on a compiler-only anomaly.
  Function addresses can affect instruction-cache and branch-predictor
  behavior even when the executed work is unchanged.
- Store durable experiments under `benchmarks/results/` with the commit,
  environment, commands, variance, and correctness checks.

The Observe-specific CLI smoke test runs only in that mode:

```console
nob --mode observe test --filter cli_metrics
```

It checks that the gated option writes versioned JSON. Tests should verify the
schema and invariants, not freeze benchmark counter totals that legitimately
change with runtime implementation work.

AllocationStats has a similarly gated report smoke test:

```console
nob --mode alloc test --filter cli_alloc_stats
```

It checks the summary and source attribution format without fixing counts or
line numbers.

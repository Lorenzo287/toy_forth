# Experiment: Interpreter-state isolation

- Date: 2026-08-01
- Baseline: committed tree before the state-isolation changes
- Candidate: per-state random, object, list-slab, and continuation storage
- OS / CPU: Windows 11 Pro / Intel Core i7-1065G7
- Compiler / build: GCC 16.1.0, Release
- Workloads: `list.toy` and `runtime-internals.toy`

The first candidate synchronized the existing process-wide object and list
pools on every acquire and release. It was correct but rejected immediately:

| Measurement | Baseline | Synchronized global pool | Difference |
| --- | ---: | ---: | ---: |
| Complete list workload, median | 112.509 ms | 176.331 ms | +56.7% |
| Repeated list concat, median | 10.689 ms | 52.221 ms | +388.5% |
| Runtime-internals workload, median | 672.800 ms | 720.140 ms | +7.0% |

The accepted design gives every state its own object, list-slab, and
continuation pools. A hidden owner pointer on boxed allocation records and the
existing slab pointer on list-node slots return released storage to the correct
state. Constructors select a pool through a nested thread-local scope; code
without a state uses direct allocation. No allocator lock remains on ordinary
execution paths.

| Measurement | Baseline | Per-state pools | Difference |
| --- | ---: | ---: | ---: |
| Complete list workload, median | 112.509 ms | 110.618 ms | -1.7% |
| Runtime-internals workload, median | 672.800 ms | 662.305 ms | -1.6% |

The baseline used five runs captured immediately before the change. The final
candidate used eight runs; timings were not alternated and should be read as
evidence that the regression is gone, not as a claimed speedup. The focused C
regression also runs four independent states concurrently through repeated
list construction, mapping, folding, and teardown.

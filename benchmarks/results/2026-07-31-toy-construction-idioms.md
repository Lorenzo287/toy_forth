# Experiment: Toy construction idioms and JSON

- Date: 2026-07-31
- Commit: `37241ad` baseline; candidate is that commit plus the source changes
  described below
- OS / CPU: Windows 11 10.0.26200 / Intel Core i7-1065G7
- Compiler / version: GCC 16.1.0
- Build configuration: Release and AllocationStats
- Commands: five fresh direct executions of
  `benchmarks/construction-patterns.toy` and `benchmarks/json.toy`; one
  `nob --mode alloc benchmark json --runs 1 --jobs 1` execution per version
- Change under test: source-only rewrite of `core/json/json.toy`; evolving
  vectors and maps stay on the stack, string fragments use persistent lists
  and one final `join`, and encoding uses `map` plus `join`. No runtime or
  language semantics changed.

The construction workload first isolates equivalent source patterns. These are
medians of five Release runs at 10,000 items:

| Result | Construction pattern | Median |
| --- | --- | ---: |
| vector | captured accumulator | 100.509 ms |
| vector | stack-threaded `fold` | 0.412 ms |
| vector | list, reverse, convert | 0.863 ms |
| vector | `infra` collection | 0.280 ms |
| map | captured accumulator | 200.674 ms |
| map | stack-threaded `fold` | 0.781 ms |
| map | pair list, then `>map` | 2.495 ms |
| string | captured accumulator | 3.894 ms |
| string | stack-threaded `fold` | 0.695 ms |
| string | one final `join` | 0.333 ms |
| string | `infra`, then `>string` | 0.538 ms |

A fetched capture is shared with the capture that retains it. Repeated updates
therefore copied the captured vector and map, while the accumulator passed
directly from one `fold` iteration to the next stayed unique. The 10,000-item
captured vector and map were respectively about 244x and 257x slower than their
stack-threaded forms. The list, `infra`, pair-conversion, and `join` alternatives
all remained linear; their suitability depends on the operations the program
needs, not only their timing in this synthetic case.

The same principle changed the scaling of the Toy-written JSON package. Values
are medians of five fresh Release processes:

| Workload | Baseline | Candidate | Difference |
| --- | ---: | ---: | ---: |
| Decode 1,000 small objects | 45.367 ms | 42.469 ms | -6.4% |
| Decode 5,000 small objects | 319.037 ms | 203.704 ms | -36.2% |
| Decode 10,000 small objects | 959.157 ms | 400.618 ms | -58.2% |
| Encode 1,000 small objects | 81.063 ms | 33.255 ms | -59.0% |
| Encode 2,500 small objects | 374.841 ms | 83.670 ms | -77.7% |
| Encode 5,000 small objects | 1,341.238 ms | 164.528 ms | -87.7% |

The growing improvement is the important signal: candidate time is close to
proportional when the input doubles, while the captured-accumulator baseline
became increasingly superlinear.

AllocationStats over all rows in `benchmarks/json.toy` supports the same
explanation:

| Measurement | Baseline | Candidate | Difference |
| --- | ---: | ---: | ---: |
| Allocation calls | 2,641,436 | 2,354,216 | -10.9% |
| Requested bytes | 4,811,240,478 | 276,606,593 | -94.3% |

This experiment changes the conclusion about JSON performance: the main limit
was not Toy's concatenative execution model or a missing mutable builder. It
was an unidiomatic ownership pattern in the Toy source. The retained rewrite is
also shorter, reducing `core/json/json.toy` from 453 to 417 lines.

The benchmark scripts perform correctness checks outside their timed regions.
The focused JSON tests and the full Release and LeakCheck suites pass, and
generated builtin metadata remains current.

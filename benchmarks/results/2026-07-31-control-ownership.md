# Experiment: Control-combinator snapshot ownership

- Date: 2026-07-31
- Baseline: commit `3837d75`
- Candidate: baseline plus snapshot-lifetime fixes in control continuations
- OS / CPU: Windows 11 10.0.26200 / Intel Core i7-1065G7
- Compiler / version: GCC 16.1.0
- Build configuration: Release
- Command: five fresh direct executions of
  `benchmarks/control-ownership.toy` with each runtime
- Change under test: stop retaining the ambient stack solely for private error
  cleanup in state-threading combinators

Each main workload repeatedly appends to one ambient vector through a control
body. Correctness checks run outside the timed regions. Values are medians of
five fresh processes:

| Workload | Baseline, 1,000 | Baseline, 10,000 | Candidate, 1,000 | Candidate, 10,000 |
| --- | ---: | ---: | ---: | ---: |
| Direct `times` threading | 0.044 ms | 0.352 ms | 0.043 ms | 0.360 ms |
| `dip` body | 2.100 ms | 227.853 ms | 0.090 ms | 0.970 ms |
| `keep` body | 2.094 ms | 214.444 ms | 0.117 ms | 1.154 ms |
| `fold` body | 0.098 ms | 0.856 ms | 0.082 ms | 0.926 ms |
| `bi` branch | 2.291 ms | 211.090 ms | 0.201 ms | 2.111 ms |
| selected `cond` body | 2.207 ms | 210.736 ms | 0.135 ms | 1.301 ms |
| updated `binrec` branch | 0.372 ms | 4.626 ms | 0.447 ms | 4.475 ms |

The repeated `dip`, `keep`, `bi`, and `cond` paths created a fresh retained
snapshot on every iteration. Their 10,000-item candidate times are about 235,
186, 100, and 162 times faster respectively, and all scale linearly. `fold`
creates one continuation for the entire traversal, so its old snapshot retained
only the initial vector and forced at most the first update to copy. Removing
it simplifies ownership without producing a meaningful timing change.

`binrec` similarly did not show a scaling cliff in this balanced split
workload. Removing its per-level rollback stack reduced the controller to the
hidden second branch required by successful execution; the 10,000-item timing
changed by about 3 percent. This is primarily a semantic and complexity
simplification rather than a speed claim.

The audit retained snapshots needed by successful execution:

- predicate evaluation, including collection predicates;
- independent projections in `app2`, `cleave`, and `construct`;
- isolated result production in `map`, `replicate`, `infra`, and `treerec`;
- the recoverable boundary owned by `try`.

It removed private error-cleanup retention from `dip`, `keep`, `fold`, `bi`,
and `binrec`, and releases `cond`'s reusable predicate snapshot before its
selected body. Thus the implementation distinction follows the language
contract: observer and projection combinators restore, while state-threading
combinators pass the current stack forward.

A follow-up consistency audit on 2026-08-01 made error propagation uniform for
the remaining control words. Observer and projection snapshots still implement
their normal successful isolation, but unwind cleanup now releases them without
restoring. Only a `try` that actually catches its body error performs rollback;
an error from its handler propagates non-transactionally. Interruption and exit
requests use the same no-rollback unwind and are not caught by `try`.

# Experiment: Two inline capture bindings

- Date: 2026-08-10
- Baseline commit: `b1322f4`
- Candidate: baseline plus allocation-site attribution and the inline-capture
  working tree
- OS / CPU: Windows 11 Pro 10.0.26200 / Intel Core i7-1065G7
- Compiler / version: GCC 16.1.0 (MSYS2 UCRT64)
- Build configurations: Release and AllocationStats
- Commands: `nob --mode alloc benchmark json --runs 1 --jobs 1` and ten
  alternating direct Release executions of fixed baseline/candidate binaries
- Change under test: keep two capture bindings in each program frame instead of
  one, retaining geometric heap storage for larger scopes

| Measurement | Baseline | Candidate | Difference |
| --- | ---: | ---: | ---: |
| JSON allocation calls | 1,659,369 | 1,153,740 | -30.5% |
| JSON requested bytes | 145,912,021 | 115,648,789 | -20.7% |
| JSON Release wall median | 924.215 ms | 903.769 ms | -2.2% |

Allocation-site attribution identified `scope_bind_var` as the dominant JSON
source: frames introducing a second capture allocated a four-entry table. A
second inline binding removes 505,629 allocations and 30,263,232 requested
bytes in the complete workload while increasing each program frame by one
`tf_var`. The fixed binaries alternated execution order; the candidate was
faster in eight of ten pairs.

Four inline bindings reduced allocation traffic further, but enlarged every
program frame enough to regress the initial Release timing comparison. Two is
the measured compromise: the runtime-internals workload showed no median
regression, and scopes with three or more names continue through the existing
heap fallback and cleanup path.

The complete GCC Release and LeakCheck suites pass, 73 tests each, and the
AllocationStats report smoke test passes. Clang compiled and linked the
instrumented runtime; the local Clang build then stopped at the pre-existing
missing `ffi.h` dependency while staging `core:ffi`.

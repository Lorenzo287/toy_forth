# Experiment: Associative quick-program cache

- Date: 2026-08-10
- Baseline commit: `4108eb1`
- Candidate: baseline plus the associative-cache working tree
- OS / CPU: Windows 11 Pro 10.0.26200 / Intel Core i7-1065G7
- Compiler / version: GCC 16.1.0 (MSYS2 UCRT64)
- Build configurations: Release, Observe, AllocationStats, and LeakCheck
- Commands: `nob --mode observe test --jobs 4`, repeated Observe
  executions of `benchmarks/json.toy`, alternating direct baseline/candidate
  executions of `benchmarks/dispatch.toy` and `benchmarks/json.toy`,
  `nob --mode alloc benchmark json --runs 1 --jobs 4`, and the complete
  Release and LeakCheck suites
- Change under test: replace the 64-entry direct-mapped quick-program cache
  with 256 entries arranged as 64 four-way sets in move-to-front order

| Observe JSON measurement | Direct 64 | Two-way 64 | Two-way 128 | Final four-way 256 |
| --- | ---: | ---: | ---: | ---: |
| Program-cache misses | 883,149 | 162,320 | 32,131 | 74-78 |
| Program-cache evictions | 883,106 | 162,270 | 32,067 | 0-5 |
| Dictionary lookups | 2,490,767 | 568,029 | 80,365 | 223-237 |

A two-way 256-entry candidate usually reached 74 misses, but one of three
fresh processes produced 76,674 misses because three hot programs shared a
set under that address layout. Four ways removed that sensitivity across five
fresh processes without increasing the 256-pointer budget. A diagnostic
4096-entry direct-mapped build also reached 74 misses and 223 lookups, so the
final cache is at the cold-miss floor for this workload.

| Release process wall | Baseline median | Candidate median | Difference |
| --- | ---: | ---: | ---: |
| Dispatch | 3827.092 ms | 3817.886 ms | -0.2% |
| JSON | 1327.977 ms | 1144.767 ms | -13.8% |

Five baseline/candidate pairs alternated process order. The machine changed
speed substantially during the run. Paired dispatch ratios ranged from 0.77
to 1.19 with a median of 1.02, while the independent medians were nearly
identical; this supports treating the primary-hit path as neutral. JSON's
paired median was 0.87, consistent with its independent-median improvement.

| AllocationStats JSON measurement | Baseline | Candidate | Difference |
| --- | ---: | ---: | ---: |
| Allocation calls | 2,154,016 | 1,659,369 | -23.0% |
| Requested bytes | 251,231,469 | 145,912,021 | -41.9% |

Each set keeps its primary way in the first compact 64-pointer region. A
primary hit therefore retains the old mask-and-load path and performs no
recency write. Overflow hits and insertions move the selected entry to the
primary way and evict the least-recently-used overflow entry. On a 64-bit
build the context-local pointer table grows by 1,536 bytes; there is no new
allocation or public surface.

Focused private coverage fills every way of one set, promotes the oldest
entry, and verifies that the next collision evicts the least-recently-used
program. The complete GCC Release and LeakCheck suites pass, 73 tests each;
the Observe suite passes all 74 tests.

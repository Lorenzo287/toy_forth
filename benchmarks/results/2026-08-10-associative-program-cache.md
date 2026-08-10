# Experiment: Associative quick-program cache

- Date: 2026-08-10
- Baseline commit: `4108eb1`
- Candidate commit: `9ed2b8f`
- OS / CPU: Windows 11 Pro 10.0.26200 / Intel Core i7-1065G7
- Compiler / version: GCC 16.1.0 (MSYS2 UCRT64)
- Build configurations: Release, Observe, AllocationStats, and LeakCheck
- Commands: `nob --mode observe test --jobs 4`, repeated Observe
  executions of `benchmarks/json.toy`, seven alternating direct
  baseline/candidate Release executions of `benchmarks/dispatch.toy` and
  `benchmarks/json.toy` with the laptop on AC power and other programs closed,
  a three-pair Release sweep of the remaining runnable Toy benchmarks, an
  eleven-pair focused `set` comparison, per-operation `set` timings, and
  fixed-base GCC relinks at four image addresses,
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
| Dispatch | 2933.841 ms | 2914.988 ms | -0.6% |
| JSON | 1001.228 ms | 916.128 ms | -8.5% |

Seven baseline/candidate pairs alternated process order. Dispatch's paired
median was +0.4% (ratios ranged from 0.97 to 1.06), supporting treating the
primary-hit path as neutral. Every JSON pair improved; its paired median was
-7.9% (ratios ranged from 0.78 to 0.94), consistent with the independent
medians. These replace an earlier battery-powered run whose CPU speed varied
too much for a useful timing comparison.

The broader sweep initially appeared to find a GCC Release regression.
Eleven focused `set` pairs measured a +11.0% paired median, localized mostly
to the copy-on-write `insert shared new` phase. Observe counters report
identical instruction, call, and frame counts. A temporary way probe counted
1,751,200 primary program-cache hits and only one overflow hit in the complete
workload, ruling out move-to-front churn.

Fresh candidate executables with byte-identical `.text` and `.rdata` sections
produced different `set` results when Windows loaded them at different ASLR
addresses. Relinking both revisions without ASLR at four shared image bases
confirmed that the apparent regression is code-address sensitive:

| Image base | Set shared-insert difference | JSON wall difference |
| --- | ---: | ---: |
| `0x140000000` | +68.6% | -6.5% |
| `0x140010000` | +2.9% | -7.7% |
| `0x140020000` | +1.3% | -9.6% |
| `0x140030000` | -0.4% | -5.8% |

The set column compares per-operation independent medians; the JSON column
compares process-wall independent medians. The set change disappears after
small shared address shifts and a Clang Profile build also did not reproduce
it, consistent with front-end or branch-predictor aliasing rather than extra
runtime work. JSON improves at every placement, so its benefit is robust to
this layout effect. No cache change is justified by the `set` result.

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

# Experiment: Cross-language runtime matrix

- Date: 2026-08-11
- Toy commit: `0c70d48`
- Joy0 commit: `5141477`; current Joy commit: `fe9fa0f`
- OS / CPU: Ubuntu 24.04.4 under WSL2 / Intel Core i7-1065G7
- Compiler: GCC 13.3.0; Toy and both Joys built with `-O3`
- Other runtimes: Lua 5.5.0 (official default `-O2` build), Python 3.12.3,
  Janet 1.41.2 (official default `-O2` build), Gforth 0.7.3, Bun 1.3.14,
  and Node 22.23.0
- Method: seven fresh processes per cell after one discarded warmup, rotating
  and reversing runtime order; executables below WSL's `/mnt/` mount were
  copied to temporary native-Linux storage before timing; medians below
- Correctness: every process produced the workload-specific checksum

Process wall-time medians in milliseconds:

| Workload | Toy | Joy0 | Joy current | Gforth | Lua | Janet | Python | Bun | Node |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Startup | 1.44 | 10.4 | 2.85 | 4.31 | 1.30 | 3.71 | 19.8 | 39.6 | 45.0 |
| Inline arithmetic | 387 | 148 | 232 | 38.6 | 56.6 | 102 | 332 | 46.8 | 64.5 |
| User-call dispatch | 558 | 176 | 348 | 39.9 | 195 | 294 | 550 | 46.7 | 64.1 |
| Fibonacci(32) | 1,169 | 254 | 440 | 67.6 | 135 | 239 | 283 | 53.3 | 72.0 |
| Sequence build + sum | 25.2 | 30.0 | 32.7 | — | 11.4 | 20.0 | 44.0 | 38.4 | 53.3 |
| String construction | 16.7 | 462 | 416 | — | 11.0 | 18.0 | 37.9 | 35.8 | 51.7 |
| Map lookup | 247 | — | — | — | 13.7 | 56.5 | 102 | 58.1 | 76.3 |

In-program CPU-time medians in milliseconds:

| Workload | Toy | Joy0 | Joy current | Gforth | Lua | Janet | Python | Bun | Node |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Inline arithmetic | 381 | 126 | 220 | 27.2 | 49.4 | 90.3 | 298 | 10.2 | 12.7 |
| User-call dispatch | 563 | 159 | 340 | 29.7 | 188 | 293 | 518 | 12.2 | 13.4 |
| Fibonacci(32) | 1,267 | 258 | 471 | 63.5 | 140 | 249 | 273 | 21.0 | 28.8 |
| Sequence build + sum | 20.0 | 18.6 | 29.0 | — | 7.44 | 12.7 | 11.0 | 5.42 | 12.3 |
| String construction | 10.4 | 457 | 444 | — | 5.57 | 7.60 | 6.17 | 6.12 | 9.07 |
| Map lookup | 260 | — | — | — | 9.10 | 52.2 | 74.8 | 29.5 | 38.7 |

All rows come from one complete matrix run. Earlier Bun process results were
dominated by paging its large executable from the Windows-mounted filesystem;
staging all mounted executables on native-Linux storage removes that artifact.
JavaScript rows still measure the first workload execution in each fresh
process, including JIT tiering, rather than warmed steady-state throughput.

## Interpretation

Toy is already a strong short-lived scripting runtime on this machine. Its
startup is second only to Lua. Its sequence and string operations are within
1.6 and 1.4 times of Janet respectively, and Toy beats current Joy on the
sequence workload while remaining over forty times faster than either Joy on
incremental string construction. These results support the existing
copy-on-write collection work and flat growable strings.

The execution loop is the clear weak area. Janet's compact bytecode VM is 4.2
times faster than Toy for inline arithmetic and 5.1 times faster on Fibonacci,
although it is only 1.9 times faster for user-call dispatch. This is useful
evidence that bytecode removes substantial per-instruction work without making
function scheduling automatically cheap. Lua is 7.7 and 9.1 times faster than
Toy on arithmetic and Fibonacci respectively.

Gforth provides a deliberately lower-level reference rather than a peer. Its
unboxed cells and threaded word execution are 14 to 20 times faster than Toy on
the three applicable rows. More revealingly, routing every increment through a
defined Gforth word adds only about 2.5 ms, while Toy's user-word version adds
about 182 ms over inline arithmetic. That isolates call/frame scheduling as a
major opportunity, but does not imply that a dynamic, refcounted Toy runtime
should match Forth directly.

Map lookup is also weak: Toy is 3.5 times slower than Python, 5.0 times slower
than Janet, 6.7 times slower than Node, and 28.6 times slower than Lua. The Toy
version uses no temporary range collections; it threads the numeric index
through `times`.

Observe counters explain much of the shape:

| Toy workload | Instructions | Program frames | Native continuations | General dispatches |
| --- | ---: | ---: | ---: | ---: |
| Inline arithmetic | 20,000,051 | 10,000,003 | 10,000,003 | 21 |
| User-call dispatch | 30,000,057 | 20,000,004 | 10,000,005 | 10,000,023 |
| Fibonacci(32) | 28,196,683 | 17,622,892 | 17,622,894 | 25 |
| Map lookup | 9,300,226 | 1,050,028 | 1,050,054 | 4,100,072 |

Dictionary lookup is not the limiting factor: the four rows together incur
only 136 dictionary lookups after quickening. The arithmetic loop instead
schedules ten million quotation frames and ten million native continuation
steps. User dispatch adds another ten million program frames and general word
dispatches. Fibonacci is dominated by recursion-controller frame scheduling.
The allocation build likewise reports only 245 process allocations for the
entire arithmetic case, so ordinary heap allocation is not its bottleneck.

The most promising next target is therefore execution scheduling: reduce or
reuse the VM work needed for repeated quotation execution and recursion
controllers. Janet confirms that a compact bytecode stream can produce a large
gain, while its dispatch row and Gforth's contrasting result show that frame
and call design remain independent concerns. For maps, the 4.1 million general
dispatches required to express one million lookups mean that both loop overhead
and map primitives deserve focused measurement before changing the
representation.

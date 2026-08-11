# Experiment: Cross-language runtime matrix

- Date: 2026-08-11
- Toy commit: `0c70d48`
- Joy0 commit: `5141477`; current Joy commit: `fe9fa0f`
- OS / CPU: Ubuntu 24.04.4 under WSL2 / Intel Core i7-1065G7
- Compiler: GCC 13.3.0; Toy and both Joys built with `-O3`
- Other runtimes: Lua 5.5.0 (official default `-O2` build), Python 3.12.3,
  Bun 1.3.14, and Node 22.23.0
- Method: seven fresh processes per cell after one discarded warmup, rotating
  and reversing runtime order; medians below
- Correctness: every process produced the workload-specific checksum

Process wall-time medians in milliseconds:

| Workload | Toy | Joy0 | Joy current | Lua | Python | Bun | Node |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Startup | 7.42 | 9.34 | 5.62 | 6.63 | 16.4 | 179 | 36.3 |
| Inline arithmetic | 381 | 144 | 229 | 64.7 | 309 | 223 | 51.4 |
| User-call dispatch | 575 | 179 | 351 | 204 | 548 | 242 | 53.5 |
| Fibonacci(32) | 1,187 | 272 | 473 | 171 | 337 | 247 | 80.5 |
| Sequence build + sum | 39.3 | 38.4 | 44.3 | 20.3 | 49.9 | 278 | 61.5 |
| String construction | 27.1 | 773 | 521 | 18.2 | 42.2 | 322 | 58.2 |
| Map lookup | 226 | — | — | 23.4 | 102 | 297 | 78.3 |

In-program CPU-time medians in milliseconds:

| Workload | Toy | Joy0 | Joy current | Lua | Python | Bun | Node |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Inline arithmetic | 375 | 122 | 217 | 49.0 | 284 | 31.6 | 12.4 |
| User-call dispatch | 560 | 157 | 332 | 184 | 510 | 56.0 | 13.1 |
| Fibonacci(32) | 1,172 | 249 | 456 | 157 | 302 | 56.4 | 27.6 |
| Sequence build + sum | 23.4 | 20.3 | 32.0 | 8.24 | 11.7 | 18.3 | 13.5 |
| String construction | 11.6 | 735 | 513 | 6.27 | 6.90 | 15.8 | 9.57 |
| Map lookup | 207 | — | — | 8.34 | 67.1 | 53.1 | 38.5 |

The arithmetic and map rows were rerun as focused seven-sample batches after
their final source adjustments; the other rows come from one complete matrix
run. Bun's large executable repeatedly became cold while runtimes alternated,
so its process wall times are dominated by loading. The in-program table is
the meaningful Bun throughput comparison.

## Interpretation

Toy is already a strong short-lived scripting runtime on this machine. Its
startup is faster than Joy0, Python, Bun, and Node. Its sequence and string
operations are within roughly a factor of two of Python and the JavaScript
JITs. Toy beats current Joy on the sequence workload and is over forty times
faster than either Joy on incremental string construction. These results
support the existing copy-on-write collection work and flat growable strings.

The execution loop is the clear weak area. Toy is 1.3 times slower than Python
for inline arithmetic and only 1.1 times slower for user-call dispatch, but it
is 3.9 times slower than Python on Fibonacci. Joy0 is 3.1 times faster on
arithmetic and 4.7 times faster on Fibonacci. Lua's reference interpreter is
7.7 and 7.5 times faster on those rows respectively. Node's optimizing JIT is
about thirty to forty times faster, which is useful context but not a near-term
target for an interpreter.

Map lookup is also weak: Toy is 3.1 times slower than Python, 5.4 times slower
than Node, and 24.8 times slower than Lua. The Toy version uses no temporary
range collections; it threads the numeric index through `times`.

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
controllers. A real replacement bytecode stream could eventually address the
same issue, but this comparison gives us a smaller hypothesis to test first.
For maps, the 4.1 million general dispatches required to express one million
lookups mean that both loop overhead and map primitives deserve focused
measurement before changing the representation.

# Experiment: Compact deferred-call storage

- Date: 2026-08-10
- Baseline commit: `486c5cd`
- Candidate: baseline plus the deferred-call storage working tree
- OS / CPU: Windows 11 Pro 10.0.26200 / Intel Core i7-1065G7
- Compiler / version: GCC 16.1.0 (MSYS2 UCRT64)
- Build configurations: Release and AllocationStats
- Commands: `nob benchmark deferred --runs 10 --jobs 1`,
  `nob --mode alloc benchmark deferred --runs 1 --jobs 1`, and eight
  alternating direct baseline/candidate executions with process order reversed
  between pairs
- Change under test: store the callable, argument count, and owned argument
  pointers in one variable-sized deferred-call allocation instead of allocating
  a linked-list node and a separate runtime vector

| Measurement | Baseline | Candidate | Difference |
| --- | ---: | ---: | ---: |
| Queue 100,000 calls median | 12.545 ms | 5.775 ms | -54.0% |
| Drain 100,000 calls median | 9.340 ms | 5.888 ms | -37.0% |
| Queue plus drain median | 22.020 ms | 11.717 ms | -46.8% |
| Process wall median | 32.130 ms | 21.140 ms | -34.2% |
| Allocation calls | 200,016 | 100,016 | -50.0% |
| Requested bytes | 10,425,552 | 3,225,552 | -69.1% |

The candidate was faster in every alternating pair. The focused queue-plus-
drain time ranged from 21.684 to 24.219 ms for the baseline and from 11.316 to
12.320 ms for the candidate. The initial ten-run Nob samples showed the same
direction, with process wall medians of 31.945 ms and 20.187 ms respectively.

The old queue retained arguments through a general-purpose refcounted vector.
For this private record the vector type, capacity, and inline storage were
unnecessary: the argument count is fixed when the call is queued, and ownership
transfers directly to the data stack when it starts. A flexible array keeps the
same FIFO order and retain/release behavior while collapsing the two live heap
objects into one.

One allocation remains per queued call. Removing it would require a context-
owned block or size-class policy for variable argument counts. The measured
single-allocation representation is already small and fast enough that such a
pool should wait for a real adapter workload demonstrating further need.

The complete GCC Release and LeakCheck suites pass, 73 tests each. They include
deferred FIFO ordering, multiple arguments, error recovery, idle host drains,
C-extension calls, cross-state rejection, and shutdown ownership.

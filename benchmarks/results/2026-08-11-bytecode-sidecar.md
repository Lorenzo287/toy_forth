# Experiment: Bytecode sidecar

- Date: 2026-08-11
- Baseline commit: `ecb98b2`
- Candidate: baseline plus uncommitted bytecode prototypes; none retained
- OS / CPU: Windows 11 Pro / Intel Core i7-1065G7
- Compiler / version: GCC 16.1.0 (MSYS2 UCRT64)
- Build configuration: Release
- Timing method: fixed baseline and candidate executables, alternating process
  order between pairs
- Change under test: lower executable quotations lazily to an opcode sidecar
  while retaining the original object vector for values, source spans, and
  debugger events

| Candidate | Workload | Pairs | Baseline | Candidate | Difference |
| --- | --- | ---: | ---: | ---: | ---: |
| 16-byte instructions | JSON | 8 | 907.208 ms | 949.639 ms | +4.7% |
| 16-byte instructions | Dispatch | 6 | 3,718.008 ms | 3,931.949 ms | +5.8% |
| 16-byte, combined call helper | JSON | 6 | 904.509 ms | 925.682 ms | +2.3% |
| 5-byte sidecar | JSON | 8 | 889.002 ms | 938.903 ms | +5.6% |
| 5-byte, direct dispatch | JSON | 8 | 880.132 ms | 919.598 ms | +4.5% |
| 5-byte, direct dispatch | Dispatch | 6 | 2,935.643 ms | 2,946.537 ms | +0.4% |

The first layout stored an object pointer, a 32-bit dictionary index, and an
opcode per instruction. Combining lookup and dispatch reduced its overhead but
did not make it faster. The compact layout stored one opcode byte and one
32-bit dictionary index per program position, reading operands from the
original quotation vector. Its final version executed cached and specialized
opcodes directly in the VM switch and invalidated them eagerly when dictionary
resolution changed. JSON was slower in every pair. The dispatch result was
noisy and effectively neutral, while still favoring the baseline median.

The prototypes passed the focused definition/redefinition, capture, and C
debug-inspection tests. Full-suite and leak validation were not run because the
performance gate rejected the change, and the runtime edits were reverted.

Toy's object vector already acts as a compact threaded instruction stream: the
object tag distinguishes calls, bindings, fetches, and literals, while the
existing quickening sidecar handles the expensive call cases. Adding a second
opcode stream duplicated hot fetch work without removing the object fetch.
This experiment therefore does not reject bytecode in general. It suggests
that a future prototype must replace the executable object stream with compact
instructions and operand/source tables, rather than layering bytecode beside
it.

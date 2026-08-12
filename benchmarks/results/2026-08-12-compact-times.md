# Compact `times` Execution

Date: 2026-08-12

## Question

Can Toy remove a meaningful part of quotation-call overhead without first
introducing bytecode or changing general call semantics?

## Change

The baseline schedules one native continuation and one program frame for every
`times` iteration. The experiment gives capture-free vector bodies a repeat
program frame: at the end of the body, the VM decrements the remaining count
and resets the same frame's program counter.

The repeat frame uses the existing program-frame layout, including otherwise
unused capture capacity for the count, so `tf_frame` does not grow. It retains
the quotation and quickening sidecar once. A small out-of-line completion
helper keeps the rare repeat check out of the ordinary dispatch loop.

The ordinary implementation remains the fallback when the body binds captures,
is a symbol or call object rather than a vector, is too large for the target's
`size_t`, or runs under a debugger hook.

## Method

- baseline: `bddb155` (`Rename toy cli`);
- candidate: branch `perf/compact-execution`;
- Windows 11 Pro 64-bit, plugged into AC power;
- Intel Core i7-1065G7;
- GCC 16.1.0 and Clang 22.1.3, Release (`-O3 -DNDEBUG`);
- fresh processes, warmed once, eight alternating baseline/candidate pairs;
- cross-language workload CPU time, measured inside Toy around the workload;
- checksums verified on every run.

## Result

Representative paired medians with GCC:

| Workload | Baseline | Repeat frame | Change |
|---|---:|---:|---:|
| arithmetic | 0.3000 s | 0.1800 s | 40.0% faster |
| user-word dispatch | 0.4795 s | 0.3785 s | 21.1% faster |
| sequence | 0.0135 s | 0.0120 s | 11.1% faster |
| string build | 0.0080 s | 0.0060 s | 25.0% faster |
| map lookup | 0.1730 s | 0.1645 s | 4.9% faster; timer noise is significant |

Clang independently showed 42.1% faster arithmetic and 25.4% faster
user-word dispatch. Alternating graph-search runs, which do not execute
`times`, showed no stable compiler-independent regression: GCC results moved
within roughly 3% in either direction as CPU frequency changed, while Clang
was slightly faster. This workload remains a useful guard against disturbing
the generic VM layout.

Observe counters identify the mechanism rather than merely correlating with
it. In the arithmetic workload, ten million scheduled program frames collapse
to one repeat frame and ten million native-continuation steps collapse to the
fixed setup/teardown work. Instruction and call counts remain unchanged. In
the dispatch workload, the repeat-body frames disappear while the ten million
user-word frames remain.

## Validation

- the complete Release suite passes;
- the complete leak-check suite passes;
- focused cases cover nested repetition, fresh capture scope per iteration,
  capture reads from an outer frame, redefinition/quickening invalidation, and
  debugger execution;
- the runtime and CLI compile and link with Clang; the complete Clang build
  remains blocked after that point by the local environment's missing
  Clang-visible `ffi.h`.

## Conclusion

Keep the experiment. It is a narrow frame specialization, not a general
bytecode architecture, but it validates the central idea: specializing a hot
execution shape can remove interpreter bookkeeping without changing the
language model. Further specializations should meet the same standard—clear
counter evidence, an unchanged generic path, and a measured end-to-end win.

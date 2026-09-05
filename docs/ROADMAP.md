# Toy Language Roadmap

This roadmap records Toy's active and future development tracks. Current
behavior belongs in the [README](../README.md) and focused references;
completed work belongs in Git history.

## Current Baseline

Toy already has the language, collection, tooling, testing, formatting, and
debugging foundations needed by the tracks below.

## Work in Progress

### Performance Work

Keep optimization work benchmark-driven and record durable experiments under
`benchmarks/results/`. Current investigation candidates are:

- dictionary lookup and word dispatch;
- allocation and object-lifetime hot spots;
- call-frame specialization;
- cache behavior for list, vector, and string workloads;
- structural hashes if map/set key policy expands.

The [native fast-path measurements](../benchmarks/results/2026-09-05-native-fast-paths.md)
and [shared capture-stack experiment](../benchmarks/results/2026-09-05-capture-stack.md)
establish baselines for the next stage. Investigate remaining continuation
scheduling, temporary boolean/float object traffic, and capture-name indexing
when many bindings remain active (capture-free depth no longer adds lookup work).
Compare further inlining against executable size, and evaluate link-time
optimization separately from portable source changes. A future bytecode
prototype should replace executable object fetches rather than add a second
instruction stream beside them.

Use the [application workloads](../benchmarks/results/2026-09-05-application-workloads.md)
alongside JSON, graph search, and the focused microbenchmarks. Separate fixture
generation from application phases, check scaling, and compare plausible Toy
implementations before attributing costs to the runtime. Coverage is still
synthetic: next useful additions include skewed real-text inputs, varied JSON
shapes and graph topologies, and a host-driven event-processing example that
exercises embedding callbacks. Let concrete consumer use determine further
workloads rather than trying to represent every language feature in one suite.

### C Interop

Embedding, C extensions, `core:ffi`, and generated bindings share one
value and ownership model, but serve intentionally different use cases. They
are usable parts of Toy; this track should improve concrete interop problems
without converging them into one universal mechanism. The C ABI is also the
common substrate through which other languages can integrate with Toy; the
project should not accumulate language-specific adapters as core features.

During the design phase, the public C API and extension ABI may change with the
language. The extension ABI version exists to reject mismatched binaries
safely, not to promise backward compatibility or require compatibility shims.

Priorities are:

1. use real embedding and library adapters to find recurring usability gaps in
   the existing boundaries;
2. grow generated bindings only with broadly reusable shapes whose conversion
   and ownership rules remain explicit; handwritten extensions remain the
   escape hatch for library-specific behavior;
3. validate deferred, fire-and-forget Toy callbacks in real adapters without
   re-entering the VM or losing its iterative execution model.

Result-bearing native continuations are explicitly deferred. Revisit them only
when a concrete synchronous C callback requires a return value and supplies
enough lifecycle constraints to design and test the boundary.

`core:ffi` should remain a small exploratory package for direct scalar and
string calls, not become privileged C syntax or a complete C interface.
Header parsing may be reconsidered if writing explicit binding manifests
becomes a demonstrated bottleneck, but it must not attempt to infer ownership
from C declarations.

Release SDKs should continue to carry everything needed for this boundary:
public headers, the embedding archive, core packages, package tools, examples,
and focused reference docs.

## Future Work

### Compiler Backends

Treat compilation as a learning track with explicit semantic checkpoints:

1. define an IR for quotations, calls, stack effects, captures, and source
   spans;
2. compile a constrained subset to bytecode for the current VM, retaining an
   interpreter fallback for dynamic behavior;
3. define the runtime ABI used by compiled code for values, errors, dictionary
   lookup, and native calls;
4. experiment with the compact [QBE compiler backend](https://c9x.me/compile/)
   after bytecode semantics are stable;
5. consider LLVM if Toy later needs broader target support or a more ambitious
   optimization pipeline.

Compilation must preserve runtime `def`, first-class quotations and symbols,
dynamic captures, predicate stack sandboxing, error behavior, and value
ownership. Early native compilation should target an explicit subset rather
than silently changing those semantics.

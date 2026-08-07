# Toy Language Roadmap

This roadmap records Toy's current development status and larger active or
future tracks. It is not a language reference, an agent manual, or a changelog.

## Status Labels

- **In progress**: an active or ongoing development track.
- **Future**: an accepted direction that has not started or is intentionally
  deferred.

## Current Baseline

Toy already has the language, collection, tooling, testing, formatting, and
debugging foundations needed by the tracks below. Current behavior belongs in
the [README](../README.md) and focused references; completed work belongs in Git
history rather than this roadmap.

## Work in Progress

### Performance Work

**Status: In progress**

Keep optimization work benchmark-driven and record durable experiments under
`benchmarks/results/`. Current investigation candidates are:

- dictionary lookup and word dispatch;
- allocation and object-lifetime hot spots;
- call-frame specialization;
- cache behavior for list, vector, and string workloads;
- structural hashes if map/set key policy expands.

### C Interop

**Status: In progress**

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
3. design callbacks and native calls into Toy without re-entering the VM or
   losing its iterative execution model.

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

**Status: Future**

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

# Runtime Internals

Toy exposes value semantics at the language level, while the runtime uses
specialized layouts, copy-on-write updates, and an explicit VM stack to avoid
unnecessary allocation, copying, and C recursion.

This document explains the current architecture and its design boundaries.
Implementation details are descriptive rather than prescriptive: verify
volatile constants and layouts in the code before relying on them.

## Execution Model

Parsed Toy code is a vector. The VM reads it from left to right: literal values
are pushed onto the data stack, call instructions look up and run words, and
nested vectors remain data until `exec` or another combinator schedules them.

Toy calls do not recurse through the C call stack. The runtime records pending
work in an explicit frame stack, and the main VM loop processes frames until no
work remains. The principal frame kinds are:

- program frames for roots, quotations, and user words;
- native continuation frames that resume a C word after scheduled Toy code,
  including compact controllers used by iteration and recursion combinators.

Program frames retain their program counter, capture scope, and any execution
sidecar needed by the active quotation; their source record supplies the
lexical package. Call instructions use the scoped dictionary and may be cached
or quickened without modifying the quotation or its debugger spans. Cache
entries are guarded by the dictionary's resolution generation so later
definitions, imports, and visibility changes remain observable.

Deferred C calls enter the same iterative execution model. They retain a
callable and arguments until the VM schedules them, and a completion
continuation prevents later events from overtaking the active handler. New
native words that execute Toy code must likewise schedule frames or
continuations rather than call `tf_vm_exec()` recursively.

The main ownership boundaries follow the source layout:

- `tf_context.c` owns context lifecycle and builtin registration;
- `tf_exec.c` owns data-stack, frame, capture, diagnostic, and VM execution;
- `tf_dictionary.c` owns word storage and lookup;
- `tf_packages.c` owns package and import registries;
- `tf_package_loader.c` loads package directories;
- `tf_debug_inspect.c` exposes read-only debugger views.

## Package Lookup

An import resolves one exact directory, canonicalizes it, and records a
per-context package entry as loading, loaded, or failed. That identity provides
load-once behavior and cycle detection. Relative paths use the importing
package directory; `core:` uses the configured core directory. There is no
fallback or environment search.

The loader parses every direct `.toy` child, verifies a common package name,
and accepts only package declarations, imports, definitions, and privacy
declarations at top level. It installs imports, extension words, source
definitions, and privacy flags in semantic order, so source filename order does
not affect package behavior.

The dictionary is keyed by package and local name. Unqualified lookup checks
the lexical package and then root native words. Qualified lookup resolves an
alias owned by the lexical package, looks up the target word, and checks its
public flag. Aliases do not copy dictionary entries, and imports are not
transitively visible.

Packages with a `toy.package` manifest load its exact extension path before
source definitions. Host-registered packages use the same registry. Native
library handles remain in the context until dependent values, frames,
definitions, and resource destructors have been released.

## Embedding Boundary

The core sources build as the static `toy_runtime` library. The `toy`
executable links it while keeping command-line parsing, the REPL, terminal UI,
and debugger protocol outside the runtime target.

`include/toy.h` is the public C boundary. Interpreter state and runtime object
layouts remain opaque. The API provides evaluation, host-to-Toy calls, native
word and package registration, package execution, stack access, persistent
value references, collection access, diagnostics, resources, interruption, and
deferred calls.

Persistent values retain their internal object and remain bound to their
originating state. Resource values wrap external pointers with copied type tags
and exactly-once destructors. C extensions include the same header with
`TOY_EXTENSION_IMPLEMENTATION`, use its versioned host function table, and do
not link a second runtime or support library. See the
[embedding guide](../embedding.md) for the public ownership and execution
contracts.

The official `core:ffi` package and generated bindings both consume this
boundary. FFI resources retain their libraries and prepared call metadata;
generated bindings emit ordinary native callbacks with range-checked stack
conversion and direct C calls.

## Debugger and Error Boundaries

A context may install a frontend-neutral debug hook. Before each Toy
instruction, the hook receives its span, program counter, and frame depth, then
chooses to step, continue, or abort. Native words are atomic at this boundary,
but quotations scheduled by native continuations produce ordinary instruction
events.

Read-only inspection APIs expose program and native frames, capture bindings,
and dictionary words without granting mutable VM access. The terminal debugger
and debug adapter share run-control logic while keeping their transports and
presentation separate.

Unhandled diagnostics inspect the live VM before unwinding and can report a
bounded stack snapshot and Toy call chain. Unwind cleanup releases frame-owned
values without reconstructing consumed inputs, so unhandled failures are
non-transactional. `try` is the exception: it owns the recovery snapshot for
its body, restores it when that body fails, and schedules the handler. Errors
from the handler continue from the handler's current stack. Interruption and
exit are not caught by `try`.

## Values and Collections

Most runtime values are refcounted `tf_obj` records. Collections retain object
references, so transformations can move or share values rather than deep-copy
them. Update-style operations mutate unique storage when safe and otherwise
clone first, preserving Toy's value semantics.

On 64-bit targets, many integers are encoded directly in the pointer-shaped
value and need no allocation or refcount operation. Values outside that range,
and all integers on 32-bit targets, remain boxed. Parsed integer literals stay
boxed so their source metadata remains available. Both representations have
identical language and public C API behavior.

The principal collection layouts are:

- **Vectors:** contiguous arrays with small inline storage, geometric growth,
  and copy-on-write updates. They also represent quotations.
- **Strings, symbols, and calls:** byte sequences with inline storage for short
  values. Indexed string reads return a new one-byte string.
- **Lists:** immutable singly linked nodes with refcounted shared tails and a
  length-caching wrapper.
- **Maps and sets:** insertion-ordered dense entries plus an open-addressed
  bucket index. Lookup is expected O(1); ordered removal is O(n).
- **Deques:** circular buffers with amortized O(1) operations at both ends.
- **Priority queues:** stable binary min-heaps that retain the original
  priority, insertion order, and value.

The runtime also keeps bounded, context-owned reusable storage for common
transient objects, continuation states, list nodes, resolution data, and
scratch snapshots. These caches are implementation details: their bounds and
inline capacities live beside their definitions in the source. Storage is
drained with its owning context, and oversized transient scratch allocations
are released instead of becoming a permanent high-water mark.

Continuation implementations retain only the inputs and stack snapshots their
successful or recoverable contracts require. Predicate combinators restore the
surrounding data stack after reading their boolean result; `try` owns recovery
state when execution is recoverable. Scratch-backed snapshots unwind in frame
order.

Each context also owns its random stream. Creating or running another context
does not reseed it. `core:random` accesses the stream through the public
extension API; the generator is intended for ordinary randomized algorithms,
not cryptographic material.

## Source Locations and Generated Metadata

Parsed values retain compact spans that share a refcounted source-file record.
Active frames borrow spans from their executable values, allowing diagnostics
and debugger views to recover locations without duplicating filenames.

`builtins.json` is the canonical builtin metadata. Its generator produces the
native declarations and registration tables under `src/generated/`, runtime
help, REPL tables, README builtin tables, LSP data, and editor word lists. Do
not edit those outputs independently.

## Measuring Changes

Use the benchmark and instrumentation modes appropriate to the question rather
than treating implementation-level intuition as evidence. The
[runtime observability guide](observability.md) covers Release timings,
deterministic counters, allocation statistics, sampled profiles, and leak
checks. Workloads live under `benchmarks/`, and durable experiments belong in
`benchmarks/results/`.

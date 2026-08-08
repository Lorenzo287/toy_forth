# Runtime Internals

Toy exposes value semantics at the language level, but the runtime uses several
specialized layouts to avoid unnecessary allocation, copying, and C recursion.

## Execution Model

Parsed Toy code is a vector. The VM reads that vector from left to right. A
literal value is pushed onto the data stack; a call instruction looks up and
runs a word; a nested vector is pushed as data until `exec` or another
combinator asks to run it.

The runtime must remember where to continue whenever one word calls another.
It records that work in *frames* on an explicit VM stack instead of relying on
the C call stack. The main loop keeps processing frames until no work remains.

- Program frames point at vector quotations and keep a program counter.
- Program frame kinds distinguish roots, quotations, and user words without
  increasing frame size. Debugger frame views recover direct-call names from
  callers and otherwise resolve user bodies through the dictionary.
- Program traversal dispatches call instructions. Symbols are pushed as name
  values; a word such as `exec` may later use a symbol to choose code to run.
- Dictionary misses hash the lexical package index together with the local word
  name. A bounded per-context cache retains resolved call/symbol objects and
  stores stable dense dictionary indexes. Hits are guarded by lexical package
  and a resolution generation changed by definitions, visibility, package
  state, and imports, so dynamic calls and later definitions observe current
  resolution without repeating hashing or string comparison.
- Program frames also use a bounded, context-local quickening sidecar keyed by
  quotation and lexical package. Each call-site slot records the resolved dense
  dictionary index and resolution generation without modifying the quotation
  or its debugger spans. Active frames retain their sidecar across cache
  eviction. Resolved `dup`, `pred`, `+`, `*`, and `<` calls quicken further to
  direct VM operations for their common stack or integer path; incompatible
  types, underflow, and overflow fall back to the canonical native word so
  diagnostics and mixed-number behavior stay unchanged.
- Native continuation frames resume C native words after a callable has run.
- Deferred C calls retain a callable and an argument vector in a context-local
  FIFO. Between instructions, the VM moves one call's arguments onto the data
  stack and places a completion continuation below its callable frame. That
  continuation prevents later events from overtaking the active handler and
  clears the active marker on success or unwind, without recursive VM entry.
- `linrec` and `genrec` keep one continuation plus a pending-unwind count
  instead of pushing one native frame per logical recursion level. `binrec`
  similarly keeps compact logical levels in one controller, retaining its four
  callables and predicate evaluator only once.
- New native words that execute user code should schedule frames or
  continuations, not call `tf_vm_exec()` recursively.
- Program and native payloads share a union because a frame is one or the other.
- Program frames keep one capture binding inline and allocate a small binding
  table only when more captures are introduced.

This model keeps Toy-level recursion and higher-order combinators independent
from the C call stack.

The implementation follows those boundaries: `tf_context.c` owns context
lifecycle and builtin registration, `tf_exec.c` owns stack/frame execution and
diagnostics, `tf_dictionary.c` owns word storage and lookup, `tf_packages.c`
owns the package and import registries, `tf_package_loader.c` scans package
directories, and `tf_debug_inspect.c` provides
read-only debugger views.

## Package Lookup

An import resolves one exact directory, canonicalizes it, and records a
per-context package entry as loading, loaded, or failed. That identity provides
load-once behavior and cycle detection. Relative paths use the importing
package directory; `core:` uses the configured core directory. There is no
fallback or environment search.

The loader parses every direct `.toy` child, verifies one common package name,
and accepts only package, import, definition, and privacy declarations at top
level. It installs all imports, then extension words, source definitions,
and privacy flags. Consequently source filename order cannot affect package
semantics.

The dictionary remains one open-addressed table keyed by `(package index,
local name)`. Each program frame recovers its lexical package from the shared
source-file record. Unqualified lookup checks that package and then root native
words. Qualified lookup resolves an alias owned by the lexical package, looks
up the target's local word, and checks its public flag. Aliases therefore do not
copy or rename dictionary entries, and imports are not transitively visible.

If a directory has `toy.package`, the loader opens its exact `extension` path and
adds the exported callbacks to the same package scope before source
definitions. Host-registered packages use the same registry already marked as
loaded. Library handles stay in the context until final teardown, after stacks,
frames, definitions, and resource destructors have been released.

## Embedding Boundary

The core sources build as the static `toy_runtime` library. The `toy`
executable links that library and keeps command-line parsing, the REPL,
linenoise, the terminal debugger frontend, and the debug protocol outside the
runtime target.

`include/toy.h` is the public C boundary. It treats the interpreter
state as opaque and exposes evaluation, host-to-Toy word calls, synchronous
native word/package registration, package import and execution, primitive
stack access, persistent value references, basic collection access,
diagnostics, and interruption. Persistent values retain their internal object
but remain state-bound, so C cannot expose or transfer `tf_obj` layouts between
runtimes. Typed resource access wraps external pointers in ordinary refcounted
objects with copied tags and exactly-once destructors, while keeping the
pointer and object layout opaque to Toy code.
The same header defines C-extension ABI version 4: an exported
descriptor entry point, a size-tagged host function table, and an
implementation-macro forwarding layer for the familiar public stack/resource
and deferred-call APIs. A C extension defines `TOY_EXTENSION_IMPLEMENTATION`
before including `toy.h` and does not link a second runtime or a separate Toy
support library.
Internal headers continue to expose implementation structures only to the
runtime and bundled frontends. See the [embedding guide](../embedding.md) for
the current ownership and execution contracts.

The official `core/ffi/` package is a consumer of this boundary rather than
part of the VM. It represents loaded libraries and prepared libffi call
interfaces as typed resources. A prepared function retains its library; Toy
resource teardown releases the call metadata and closes the foreign library
before the native `ffi` package itself is unloaded.

Generated bindings take the other route through the same package boundary.
`tools/generate-binding.js` emits ordinary native callbacks that perform
range-checked stack conversion and direct C calls. The generated translation
unit instantiates the C-extension portion of `toy.h`, so it can be compiled
directly against the foreign library while still sharing the host VM.

## Debugger Hooks

The VM can install a frontend-neutral debug hook on a context. Before each Toy
instruction, the hook receives the instruction, source span, program counter,
and frame depth. It returns one of three actions:

- step executes that instruction and pauses before the next Toy instruction;
- continue suppresses further pauses for the current VM invocation;
- abort unwinds the invocation with interruption cleanup.

Native words remain atomic at this boundary. Native continuations are visible
in read-only frame views, and any Toy quotations they schedule produce normal
instruction events. Frame views expose stable word labels, PCs, program lengths,
call sites, current locations, and borrowed capture bindings without giving
frontends mutable VM access. Similar read-only views distinguish native and
user-defined dictionary entries and expose user-word bodies. These views reuse
state the VM already retains; normal instruction dispatch does no extra capture
or dictionary bookkeeping for the debugger.

The first frontend is the terminal debugger, tdb, available from the REPL and
through `--tdb` for files and evaluated source. A machine frontend uses the
same hook for `toy-dap`. Its private record-separated stream multiplexes paused
snapshots with ordinary Toy output; the Go adapter translates those records to
DAP without parsing tdb's human-oriented text. Both frontends use the internal
debug controller for step-in/over/out state and source or word breakpoint
filtering, while retaining their own command parsing and presentation.

Unhandled diagnostics inspect the same live VM state before error unwinding.
They print a bounded data-stack snapshot and, when execution is nested, a
bounded call chain of Toy program frames. Native continuation frames remain an
implementation detail in these user-facing reports. Unwind cleanup releases
frame-owned values and snapshots without reconstructing consumed inputs, so
unhandled errors have uniformly non-transactional stack effects.

Error suppression used by `try` prevents handled failures from producing
diagnostics, copies the stored error message onto the restored data stack, and
clears the handled context before scheduling its handler. That restoration
happens only when `try` catches its body error; an error from the handler
continues unwinding from the handler's current stack. Interruption and exit use
the same non-transactional unwind cleanup and are not caught by `try`.

## Object Layout

Most runtime values are boxed `tf_obj` records with reference counts.
Collections store retained `tf_obj *` values, so most collection
transformations move or share references rather than deep-copying values.

On 64-bit targets, integers from -2^62 through 2^62-1 are encoded directly in
the `tf_obj *` value using its otherwise-zero low alignment bit. Retaining or
releasing one of these immediate integers is a no-op. Integers outside that
range remain boxed, so Toy still exposes the complete signed 64-bit range and
boxed and immediate integers have identical equality, hashing, display, and C
API behavior. On 32-bit targets all integers remain boxed.

Parsed integer literals are also kept boxed even when they fit the immediate
range. Instructions own debugger source spans, while dynamically produced
integers do not need that metadata. Arithmetic, ranges, indexed lengths, and
the embedding API can therefore avoid transient integer allocations without
weakening source-level diagnostics.

Important object-level optimizations:

- vectors keep two elements inline inside the object before allocating an
  external element array;
- short strings, symbols, and call nodes store bytes inline inside the object;
- heap strings reuse otherwise idle inline bytes to remember capacity;
- released boxed `tf_obj` records can be reused through a bounded object
  cache;
- resource objects run their external destructor before their released object
  storage enters that cache;
- many update-style words mutate only when `refcount == 1`, otherwise they
  clone first to preserve value semantics.
- vector `filter` results keep up to two matches inline, then reserve at most
  64 slots based on input length to avoid repeated geometric growth without
  retaining unbounded excess capacity for sparse results.

These choices are intentionally invisible to Toy code.

## Collection Layouts

### Vectors

Vectors are arrays and are also the quotation representation. They use inline
storage for very small vectors, geometric reserve for repeated growth, and
copy-on-write for update-style words such as `set-at`, `push-back`,
`pop-back`, and `concat`.

Back pops reduce length but do not shrink capacity. That keeps vector-as-stack
workloads amortized O(1) instead of paying reallocation costs on alternating
push/pop patterns.

### Strings, Symbols, and Calls

Strings, symbols, and call instructions all store byte sequences. Short values
fit inside the object itself; longer values use one heap allocation. Exact-size
producers allocate the final storage once, while dynamic string producers grow
geometrically and transfer their buffer into the final object.

Indexed byte reads are O(1), but they return a new one-byte string because a
Toy character is itself a string value.

### Lists

Lists are immutable singly linked values. A list wrapper caches length, and
nodes are independently refcounted, so `rest` and `uncons` can return a new
wrapper that shares the suffix. This makes front operations cheap while keeping
list values persistent.

### Maps and Sets

Maps and sets use two coordinated arrays:

- a dense insertion-order entry array;
- an open-addressed bucket array storing one-based entry indexes.

The dense array gives deterministic projection/display order. The bucket array
gives expected O(1) lookup. Removing a present entry shifts the dense array and
rebuilds bucket indexes, which is why ordered removal is O(n).

### Deques

Deques use a circular buffer. The head index can move without shifting
elements, so front and back pushes/pops are amortized O(1). Shared deques are
cloned before updates.

### Priority Queues

Priority queues use a binary min-heap. Each entry stores:

- the original priority object;
- a monotonic insertion order for stable equal-priority behavior;
- the value object.

`>pqueue` reserves once and heapifies bottom-up in O(n). `pairs` clones and
pops the heap to emit public priority order without exposing the internal heap
layout.

## Source Locations

Parsed values retain compact source spans. All spans from one parse share a
refcounted source-file record, so a filename is allocated once rather than once
per token. Program and native frames borrow spans from executable values while
the owning program remains active.

## Builtin Metadata

Builtin metadata is generated from `builtins.json`.

Generated outputs include:

- native entry-point declarations in
  `src/generated/tf_builtins_decls.inc`;
- grouped native registration tables in `src/generated/tf_builtins.inc`;
- runtime docs in `src/generated/tf_docs.c`;
- REPL builtin declarations in `src/generated/tf_repl_builtins.inc`;
- README builtin tables;
- LSP builtin docs;
- Tree-sitter and VS Code builtin word lists.

This keeps help output, documentation, editor tooling, and native registration
from drifting apart.

## Bounded Reuse

The runtime keeps bounded reusable storage for common transient work:

- up to 256 released `tf_obj` records per interpreter state;
- up to 64 KiB of completely empty 128-node list slabs per state;
- up to 128 continuation-state blocks of 512 bytes per interpreter state;
- up to 64 retained call/symbol keys in the word-resolution cache;
- up to 64 cached quickened quotation/package sidecars, plus any retained by
  active program frames;
- one 4 KiB scratch block per active context and up to 64 KiB of spare scratch
  blocks.

Object, list-slab, continuation, and scratch storage all belong to their
interpreter state and are drained when it closes. Boxed allocation records keep
their owning pool outside the public `tf_obj` layout, while list nodes already
carry their slab ownership. A scoped thread-local selector tells constructors
which state owns new storage; release always follows the recorded owner, and
construction without a state uses direct allocation. The bounds prevent a
short-lived high-water workload from retaining unbounded memory while removing
allocator traffic from normal scalar execution and combinator loops.

Persistent-list nodes are allocated from 128-node slabs. Each slab tracks its
own free nodes and live-node count, so independently refcounted shared tails can
outlive other nodes from the same slab. Completely empty slabs are retained up
to the spare-byte limit; excess empty slabs are returned immediately. Cache
cleanup frees the owning state's empty slabs without touching another state.

Each interpreter also owns an explicit PCG32 random stream. State creation seeds
it from operating-system entropy, with a mixed unique fallback if entropy is
unavailable. Creating or executing another state never reseeds that stream.
The `core:random` C extension reaches it through the public extension host API;
no random implementation primitives enter Toy's global word dictionary. The
range helper joins two 32-bit draws and uses rejection sampling, so signed
half-open ranges do not inherit modulo bias. The generator is suitable for
ordinary randomized algorithms but not cryptographic material.

Predicate continuations keep up to 16 surrounding stack references inline and
fall back to exact per-context scratch storage for deeper stacks. Collection
predicates reuse an invariant surrounding-stack snapshot across iterations.

Control combinators whose successful contract restores or independently
projects a surrounding stack keep up to 32 references in their cached
continuation state and use the same scratch storage for deeper stacks. Scratch
allocations carry their rewind mark and must be released in native-frame order;
nested and error-unwound continuations are strictly LIFO. Compile-time size
checks keep inline states within the 512-byte control-state cache block.

State-threading continuations such as `dip`, `keep`, `fold`, and `bi` keep only
the hidden inputs and stack lengths required by their successful result shape.
They do not retain the ambient stack solely for unhandled-error cleanup. A
`try` frame owns the snapshot when execution is recoverable.

Scratch blocks larger than the spare-byte limit are freed as soon as their
owning frame releases them, so an unusually deep stack does not become a
permanent context high-water mark.

The `binrec` controller keeps only the hidden second-branch value in each
compact logical level. It does not retain the threaded stack for private error
rollback; an enclosing `try` owns recovery state when execution is recoverable.

## Measuring Changes

Use the benchmark suite rather than isolated impressions:

```console
nob --mode alloc benchmark
```

The `alloc` mode reports checked allocation calls and cumulative
requested bytes. These totals compare identical workloads; they are not live or
peak memory. Timing and allocation workloads live in `benchmarks/`, including
`runtime-internals.toy` for continuations, captures, predicate snapshots, and
recursion schemes.

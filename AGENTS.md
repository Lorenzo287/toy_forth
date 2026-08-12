# Toy: Agent Guide

Toy is a small concatenative language and C runtime with quotations, symbols,
dynamic captures, directory packages, refcounted collections, and an iterative
VM frame stack.

This guide highlights project-specific constraints and useful starting points.
It is not a substitute for inspecting the current code and tests. Treat the
invariants below as requirements unless the task changes the underlying design;
adapt the other guidance when local evidence supports a better approach.

## Find the Relevant Context

| Task | Start with |
| --- | --- |
| VM execution, stacks, captures, or diagnostics | `src/tf_exec.h`, `src/tf_exec.c`, `docs/development/runtime-internals.md` |
| Parsing or syntax | `src/tf_parser.h`, `src/tf_parser.c`, focused cases in `tests/toy/` |
| Native words | `builtins.json`, `src/tf_builtins.h`, `src/tf_builtins_*.c` |
| Packages and imports | `src/tf_packages.c`, `src/tf_package_loader.c`, `docs/packages.md`, `tests/packages/` |
| Public C API, embedding, or extensions | `include/toy.h`, `src/toy.c`, `src/tf_native_loader.c`, `docs/embedding.md` |
| CLI, REPL, or debugger | `src/cli/`, `src/tf_debug_control.*`, `src/tf_debug_inspect.c` |
| Builds and tests | `docs/development/build.md`, `docs/development/testing.md`, `nob.c` |
| Performance work | `docs/development/observability.md`, `benchmarks/`, `benchmarks/results/` |
| Current and future project tracks | `docs/ROADMAP.md` |

User-visible behavior is documented in the README and focused references under
`docs/`. Examples show consumer workflows; tests define regressions. Search the
repository for nearby implementations and tests before assuming the table above
is exhaustive.

## Repository Invariants

- `builtins.json` is the canonical builtin metadata. Do not hand-edit generated
  registry, declaration, runtime-doc, REPL, README-table, LSP, Tree-sitter, or
  VS Code outputs. Run `node tools/generate-builtins.js` after metadata changes
  and validate with its `--check` mode.
- Native words that execute Toy code schedule VM frames or native
  continuations and return to the VM loop. Do not introduce synchronous
  `tf_vm_exec()` re-entry.
- Runtime code retains object references when storing them and releases them
  when finished. Use the `tf_xmalloc` allocation helpers.
- Release SDK contents under `dist/toy` are consumer-facing. Installed docs,
  examples, and tools must not depend on Nob or repository-only paths.

Generated files under `src/generated/` are outputs. Tree-sitter's tracked
`tools/tree-sitter-toy/src/` is also generated; regenerate it with
`npm run generate` from that directory after changing its generated word list.

## Working Defaults

Consult the relevant reference and focused tests before changing language
behavior. Keep stack effects, ownership, error behavior, and complexity visible
in new or changed APIs. Prefer the language's existing quotation and combinator
model when it already expresses the intended semantics; new syntax or vocabulary
should solve a distinct user problem.

Bootstrap Nob with `cc nob.c -o nob` (or the platform-equivalent compiler
command), then use `nob build` and `nob test`. Run focused tests while iterating
and use `--mode leak` for ownership, stack-effect, or execution-flow changes.
Use `nob dist` for the staged SDK and `nob benchmark` for measured performance
work; the linked development guides contain the supported modes and workflows.

Record current status and future work in `docs/ROADMAP.md`, durable performance
experiments in `benchmarks/results/`, and completed work in Git history.

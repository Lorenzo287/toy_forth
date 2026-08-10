# Toy: Agent Manual

Toy is a small concatenative language/runtime in C. It has a stack-based
execution model, first-class quotations and symbols, explicit call nodes,
refcounted collection objects, dynamic captures (`| a b |` / `$a`), directory
packages with `.` qualified public words, a generated builtin registry, a scoped
word dictionary, and an iterative VM frame stack for user words.

Roadmap work lives in `docs/ROADMAP.md`. Keep this file focused on
navigation and development rules.

## Project Map

- `builtins.json`: canonical builtin metadata used to generate registry, docs,
  runtime help, LSP data, Tree-sitter word lists, and VS Code grammar data.
- `src/`: interpreter implementation and private `tf_*.h` APIs; read the
  relevant private headers before engine, parser, object, or native edits.
- `src/cli/`: standalone executable, REPL, and debugger-protocol frontend.
- `src/generated/`: generated builtin declarations/registry, runtime docs, and
  REPL word tables; do not hand-edit.
- `include/toy.h`: single public embedding and standalone C-extension header.
- `examples/`: standalone Toy programs run with `toy --file`; formatting-
  sensitive quines live under `examples/quines/`.
- `examples/graphs/`: executable, tested BFS and Dijkstra examples and their
  demonstration package.
- `examples/embedding/`: C hosts that embed and call the Toy runtime.
- `examples/ffi/`, `examples/packages/`: dynamic FFI, generated binding, and
  handwritten C-extension examples. Library-specific adapters are test
  cases for the general boundary, not built-in integrations.
- `core/`: official packages maintained and built with Toy.
- `core/json/`: strict Toy-written JSON codec and generated editor metadata.
- `core/random/`: hybrid C/Toy random ranges, choices, and shuffling over the
  state-owned generator.
- `tests/packages/`: source, core, and C-extension integration fixtures.
- `tests/toy/`, `tests/c/`: language cases and C API regressions. Toy test
  prefixes declare behavior: `test_`, `fail_`, `output_`, and `manual_`.
- `docs/`: user-facing language, package, interop, and editor docs.
- `docs/core/`: focused references for official core packages.
- `docs/development/`: source build, testing, and runtime implementation notes.
- `docs/combinators.md`: examples for nontrivial control, recursion, and
  collection combinator usage.
- `docs/idioms.md`: preferred source patterns for combinators, captures,
  construction state, sharing, and collection choice.
- `docs/data-model.md`: collection syntax, interop, complexity, equality, and
  hashing reference.
- `docs/packages.md`: directory packages, exact imports, executable entry
  points, and C-backed package shape.
- `docs/c-libraries.md`: user guide for dynamic FFI, generated bindings, and
  handwritten C extensions.
- `docs/development/runtime-internals.md`: VM/object/allocation implementation
  notes.
- `docs/development/observability.md`: compile-gated metrics, allocation,
  profiling, and performance-measurement workflow.
- `docs/embedding.md`: C embedding and native-word API.
- `docs/editor.md`: formatter, LSP, DAP, Tree-sitter, VS Code, and editor setup.
- `benchmarks/`: reproducible performance workloads run by `nob benchmark`.
- `benchmarks/results/`: benchmark result notes and comparison templates.
- `tools/generate-builtins.js`: builtin metadata generator and consistency
  checker.
- `tools/tree-sitter-toy/`: Tree-sitter grammar, tracked generated parser,
  queries, Go binding, and grammar tests.
- `tools/go.mod`, `tools/cmd/`, `tools/internal/`: Go module containing the
  installed LSP, DAP, formatter, C-extension, and binding frontends plus their
  shared implementations.
- `tools/vscode-toy/`: VS Code extension and generated grammar metadata.
- `.github/workflows/release.yml`: tag-driven release automation.
- `tools/install.ps1`, `tools/install.sh`: general installers copied
  into staged release SDKs; they consume built artifacts and must not rebuild
  repository tools.
- `nob.c`: self-contained repository build entry point for the runtime, CLI,
  examples, tests, and staged SDK distributions; `tools/nob/build.h` contains
  compiler and distribution helpers, while `tools/nob/tests.h` contains the
  isolated test harness.
- `deps/`: vendored `linenoise`, `stb_leakcheck`, and `nob`.

## Fast Context

- Builtin source of truth: `builtins.json`; run
  `node tools/generate-builtins.js` after edits and use `--check` in validation.
- Native word registry: generated grouped tables in
  `src/generated/tf_builtins.inc`,
  included by `src/tf_context.c` and registered by `tf_ctx_new()`.
- Native declarations: `src/tf_builtins.h`; implementations are grouped in
  `src/tf_builtins_*.c`.
- Public embedding, package-registration, and standalone C-extension API:
  `include/toy.h`, implemented by `src/toy.c`; platform extension loading:
  `src/tf_native_loader.c`.
- Libffi core package: `core/ffi/toy_ffi.c`; user workflow, signatures, and
  safety contract: `docs/c-libraries.md`.
- Toy-written JSON core package: `core/json/json.toy`; mapping and strictness
  contract: `docs/core/json.md`.
- Hybrid random core package: `core/random/toy_random.c` and
  `core/random/random.toy`; range, seeding, and sequence-operation contract:
  `docs/core/random.md`.
- Explicit-manifest binding generator: `tools/generate-binding.js`; installed
  frontend: `toy-bindgen`; C-extension compiler: `toy-c-package`; manifest
  reference: `docs/bindgen.md`.
- External C-extension examples: `examples/packages/raylib/toy_raylib.c`
  and `examples/packages/sqlite/toy_sqlite.c`.
- Context lifecycle and builtin registration: `src/tf_context.c`; stack,
  frames, diagnostics, captures, and VM dispatch: `src/tf_exec.h`,
  `src/tf_exec.c`.
- Dictionary lookup: `src/tf_dictionary.c`; package registry:
  `src/tf_packages.c`; package loading and exact path resolution:
  `src/tf_package_loader.c`.
- Shared debugger run control: `src/tf_debug_control.h`,
  `src/tf_debug_control.c`.
- Read-only debugger frame, capture, and word views: `src/tf_exec.h`,
  `src/tf_debug_inspect.c`.
- Objects/ownership: `src/tf_obj.h`, `src/tf_obj.c`, `src/tf_alloc.h`.
- Parser: `src/tf_parser.h`, `src/tf_parser.c`.
- Terminal capability and ANSI color handling: `src/tf_terminal.h`,
  `src/tf_terminal.c`.
- REPL: `src/cli/tf_repl.h`, `src/cli/tf_repl.c`.
- Language plan: `docs/ROADMAP.md`.
- Package model: `docs/packages.md`.
- C library boundaries: `docs/c-libraries.md`.
- Data model reference: `docs/data-model.md`.
- Toy source idioms: `docs/idioms.md`.
- Test conventions: `docs/development/testing.md`.
- Performance measurement: `docs/development/observability.md`.
- Editor tooling and formatter behavior: `docs/editor.md`.

## Workflow

- Start with `git status --short`; do not overwrite user changes.
- For language behavior, check `README.md`, then the relevant C files and tests.
- For roadmap work, read `docs/ROADMAP.md` first. For collection or
  data-structure work, read `docs/data-model.md` too.
- For native word changes, update `builtins.json`, declarations, and focused
  `tests/toy/` cases, then regenerate and commit all generated metadata.
- Bootstrap the build with `clang nob.c -o nob.exe`; use
  `.\nob.exe build` and run `.\nob.exe test` for the default suite. Use
  `--mode leak` for ownership, stack-effect, or execution-flow changes.
- Use `.\nob.exe dist` to stage the consumer SDK at `dist/toy`. User-facing docs and examples
  invoke `toy`, `toy-c-package`, `toy-bindgen`, and the installed editor tools;
  they must not depend on Nob or repository build paths.
- Use `.\nob.exe benchmark` for performance workloads; names and `--runs`
  select focused samples.

## Development Rules

- C style: snake_case, 4-space indentation. Use `tf_` for exported/project-wide
  symbols; file-local `static` helpers may use unprefixed file-local names.
- Memory: `tf_obj_retain` when storing references, `tf_obj_release` when done,
  use `tf_xmalloc` helpers.
- Native callable runners should schedule frames or native continuations and
  return to the VM loop. Do not add synchronous `tf_vm_exec()` re-entry for new
  native words.
- Language direction: settle semantics before adding syntax. Prefer
  quotation-first words and combinators over new syntax.
- Toy source: use combinators as the default control vocabulary. Captures are
  best for stable inputs and context; thread evolving aggregate state on the
  stack so copy-on-write collections can remain unique. Choose specialized
  structures for operations the program actually needs; consult
  `docs/idioms.md` for examples and benchmark evidence.
- Vocabulary: overload an existing word when the language concept is the same
  across types. Avoid aliases that expose only implementation categories. New
  words need clear stack effects, focused tests, and a real use case.
- Stack effects: ordinary words consume their declared inputs. Predicate
  quotations in control/predicate combinators restore the surrounding data stack
  after reading a boolean result; side effects inside them are not undone.
- Data behavior: represent absence with a predicate, caller-provided default,
  or runtime error rather than an unrelated sentinel. Update-style words return
  values and must not expose shared in-place mutation.
- Sequence words should be uniform across vectors, lists, and strings when the
  result type is clear. Keep one-pass operations O(n) for every finite sequence
  and document capability-specific complexity differences. Strings are byte
  sequences; a string item is a one-byte string.
- Numeric behavior: integers are signed 64-bit values and floats are doubles.
  Mixed comparisons must preserve exact integer ordering where possible.
- Tooling: builtin metadata is generated from `builtins.json`; do not hand-edit
  generated C declarations/registry, runtime docs, LSP data, Tree-sitter word
  lists, VS Code grammar, or README table outputs. Regenerate the Tree-sitter
  parser after generated word-list changes when the CLI is available. Use
  `npm run generate` from `tools/tree-sitter-toy`; its tracked `src/` directory
  is also the Go parser package.
- Docs: README and its focused references describe current user-visible
  behavior; AGENTS contains repository navigation and durable development
  rules; the roadmap contains only current status, sequencing, and future work.
  Use Git history rather than the roadmap as a changelog.
- Shell: assume Windows PowerShell; use familiar aliases such as `cd` where
  available and do not output Bash-only syntax.

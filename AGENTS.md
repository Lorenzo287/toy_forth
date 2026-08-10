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
- `src/`: interpreter implementation and private `tf_*.h` APIs.
- `src/cli/`: standalone executable, REPL, and debugger-protocol frontend.
- `src/generated/`: generated builtin declarations/registry, runtime docs, and
  REPL word tables; do not hand-edit.
- `include/toy.h`: single public embedding and standalone C-extension header.
- `core/`: official Toy and hybrid C/Toy packages.
- `examples/`: standalone programs, embedding hosts, FFI, and package examples.
- `tests/toy/`, `tests/c/`: language cases and C API regressions. Toy test
  prefixes declare behavior: `test_`, `fail_`, `output_`, and `manual_`.
- `tests/packages/`: source, core, and C-extension integration fixtures.
- `docs/`: user-facing references; `docs/development/` covers building,
  testing, runtime internals, and observability.
- `benchmarks/`: reproducible performance workloads run by `nob benchmark`.
- `benchmarks/results/`: benchmark result notes and comparison templates.
- `tools/`: generators, Go editor/build frontends, Tree-sitter grammar, and
  VS Code extension.
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
- Focused references: `docs/data-model.md`, `docs/idioms.md`,
  `docs/packages.md`, `docs/c-libraries.md`, `docs/embedding.md`, and
  `docs/editor.md`.
- Development references: `docs/development/testing.md`,
  `docs/development/runtime-internals.md`, and
  `docs/development/observability.md`.

## Workflow

- Preserve existing work and keep unrelated changes intact.
- Consult the relevant user-facing reference and tests before changing language
  behavior. Keep roadmap work in `docs/ROADMAP.md`.
- For native word changes, keep `builtins.json`, declarations, focused tests,
  and generated metadata in sync. Run `node tools/generate-builtins.js` after
  metadata edits and use `--check` in validation.
- Bootstrap the build with `clang nob.c -o nob.exe`; use
  `.\nob.exe build` and `.\nob.exe test` for the default suite. Use
  `--mode leak` for ownership, stack-effect, or execution-flow changes.
- Use `.\nob.exe dist` to stage the consumer SDK at `dist/toy`. User-facing docs and examples
  invoke `toy`, `toy-c-package`, `toy-bindgen`, and the installed editor tools;
  they must not depend on Nob or repository build paths.
- Use `.\nob.exe benchmark` for performance workloads; names and `--runs`
  select focused samples.

## Development Rules

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

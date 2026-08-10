# Developing Toy from Source

## Bootstrap Nob

Toy vendors [Nob](https://github.com/tsoding/nob.h). Bootstrap its single source
file with a C compiler:

```console
cc nob.c -o nob
```

Nob rebuilds itself when its source or included build headers change.
The normal source build also needs libffi headers and a linkable
`ffi` library because it builds the official `core:ffi` package.

## Development Commands

```console
nob build
nob test
nob test --filter package
nob dist
nob clean
```

`build` produces the Toy CLI, the `toy_runtime` static archive, and `core:ffi`
under `build/<compiler>/<mode>/`. Run the checkout's interpreter directly:

```console
build/gcc/release/toy --file examples/factorial.toy
build/gcc/release/toy --file examples/quines/quine.toy
```

`test` runs isolated Toy, package, debugger, embedding, native-loader, and
binding-generator regressions. It does not provide a separate way to build
user examples.

`dist` stages the consumer SDK at `dist/toy`. It
builds the runtime and precompiled Go tools, then copies public headers, core
packages, generator and Tree-sitter assets, examples, docs, and installers.
Release archives contain that directory unchanged. Generated Tree-sitter
sources are tracked, so a clean checkout can build the SDK directly.

The source checkout intentionally has no root installer. Run
`powershell -File dist/toy/install.ps1` on Windows or
`sh dist/toy/install.sh` on Unix after staging. For C extensions and generated
bindings, use the installed SDK guides in
[Using C Libraries](../c-libraries.md) and the
[Binding Manifest Reference](../bindgen.md).

## Compilers and Modes

Select a compiler and mode with:

```console
nob --cc gcc --mode debug build
```

Supported compilers are `clang`, `gcc`, `msvc`, and `clang-cl`. On Windows,
`msvc` requires a Visual Studio Developer PowerShell. Supported modes are:

- `release`: optimized build;
- `debug`: unoptimized build with debug information;
- `alloc`: optimized build with allocation counters;
- `leak`: leak instrumentation (`stb_leakcheck` on Windows, LeakSanitizer on
  supported Unix compilers);
- `observe`: optimized build with compile-gated deterministic runtime metrics;
- `profile`: optimized build with symbols and frame pointers.

Independent C compilations run in parallel. Use `-j` or `--jobs` to change the
default worker count. Profiling with MinGW GCC on Windows is not supported.

```console
nob --mode alloc benchmark dispatch
nob --cc clang --mode profile build
samply record build/clang/profile/toy --file benchmarks/runtime-internals.toy
```

## Runtime Metrics

The `observe` mode defines `TF_OBSERVE`; normal Release builds do not contain
the metrics fields, counter operations, JSON writer, or CLI option. Write one
context's cumulative VM and dispatch counters with:

```console
nob --mode observe build
build/gcc/observe/toy --metrics-json metrics.json --file workload.toy
```

The versioned JSON contains instruction, word, frame, stack high-water,
dictionary-cache, quick-program-cache, and specialized-dispatch counts. These
deterministic counters attribute runtime behavior but perturb timing. Use a
Release or Profile build for elapsed-time conclusions.

## libffi

The normal build compiles `core/ffi/toy_ffi.c` and links it with libffi. If the
headers or library are outside the compiler's default paths, pass their
locations to `build`, `test`, or `dist`:

```console
nob --cc gcc build --include path/to/libffi/include --lib-dir path/to/libffi/lib --lib ffi
```

With no `--lib` option, the build links `ffi`. Supplying one or more `--lib`
options replaces that default list, which permits `--lib libffi` or an exact
library path on unusual installations. The selected compiler must match the
libffi distribution; MSYS2/MinGW libffi normally uses `--cc gcc`. See
[Using C Libraries](../c-libraries.md#dynamic-ffi) for its runtime dependency
and safety constraints.

## Tooling

The Go module for the installed commands lives directly under `tools/`. Run its
tests or an individual command from the repository root:

```console
go -C tools test ./...
go -C tools run ./cmd/toy-lsp
```

Grammar changes begin in `tools/tree-sitter-toy/`. Its generated `src/`
directory is tracked and is also the Go parser package, so one generated parser
serves the SDK and language server:

```console
cd tools/tree-sitter-toy
npm ci --ignore-scripts
npm rebuild tree-sitter-cli
npm run generate
npm test
```

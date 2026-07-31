# Installing Toy

Toy releases are complete, relocatable SDK directories. A release can run Toy
programs, build C extensions, generate bindings, provide editor tooling, and
support C embedding without a repository checkout or the Nob build system.

## Release Installation

Download and extract the archive for your platform. The archive contains a
single `toy` directory. On Windows, install it for the current user with:

```console
powershell -File toy/install.ps1 -AddToPath
```

Without `-AddToPath`, the script copies the SDK but only prints the directory
that should be added to `PATH`. Choose a custom destination with `-InstallDir`.

On Linux or macOS:

```console
sh toy/install.sh
```

This installs the SDK under `$XDG_DATA_HOME/toy`, or
`$HOME/.local/share/toy`, and creates command links in `$HOME/.local/bin`.
Use `--prefix`, `--bin-dir`, or `--no-links` to change those defaults.

The archive is also portable: adding its `bin` directory to `PATH` is enough.
Verify either installation with:

```console
toy --help
toy --eval "20 22 + print"
```

## SDK Layout

```text
toy/
|-- bin/       Toy, formatter, LSP, DAP, bindgen, and C-extension builder
|-- core/      version-matched core: packages
|-- include/   the public embedding and C-extension header
|-- lib/       the static embedding runtime for the release compiler
|-- share/     generator payload and Tree-sitter grammar assets
|-- examples/  language, embedding, FFI, and C-extension examples
`-- docs/      user-facing reference documentation
```

The tools discover this root relative to their executable. `TOY_ROOT` overrides
SDK discovery for `toy-bindgen` and `toy-c-package`; it does not add an import
search path. The interpreter continues to resolve ordinary imports exactly and
accepts `--core-path` when an embedder or unusual installation stores `core`
elsewhere.

`toy-bindgen` requires Node.js, but it uses the generator shipped in
the same SDK rather than a repository copy. `toy-c-package` requires a C
compiler and accepts `--cc` plus explicit include and library options. Normal
Toy source packages require neither tool. A package with a C extension contains
compiled code in a `.dll`, `.so`, or `.dylib`; its manifest names that library
with `extension`. The optional `core:ffi` package may also require the platform's
libffi shared library at runtime; [Using C Libraries](./c-libraries.md#dynamic-ffi)
describes that dependency. The Toy-written `core:json` package has no external
runtime dependency.

## Installed and Project Packages

The SDK's `core` directory is versioned with Toy and should be treated as
read-only. Project dependencies do not belong in the SDK root: keep them at
exact project-relative paths such as `vendor/`, then import those directories.
Toy packages live at exact project-relative paths, commonly under `vendor/`,
rather than in a registry or mutable global package area. The SDK's `examples`
directory is a read-only source of templates: run pure Toy examples there if
useful, but copy embedding and interop examples into a project before compiling
or generating files.

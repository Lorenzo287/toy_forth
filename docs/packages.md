# Packages and Imports

Toy packages are directories. The directory is the unit of loading and
namespacing; files only split its implementation.

## A Source Package

Every `.toy` file directly inside one package begins with the same declaration:

```text
project/
|-- app/
|   `-- main.toy
`-- math/
    |-- arithmetic.toy
    `-- constants.toy
```

```toy
\ math/arithmetic.toy
'math package

'double [ 2 * ] def
'helper [ 1 + ] def
'helper private
```

```toy
\ app/main.toy
'main package
"../math" import

'main [ 21 math.double print ] def
```

Run the executable package by passing its directory to Toy:

```console
toy project/app
```

An executable package is named `main` and exports a public `main` word. That
word has no special syntax or implicit parameters. Arguments after the package
directory are available through `argc` and `argv`.

## Declarations and Visibility

Package files contain declarations at top level:

- `'name package` first in every file;
- `"path" import` or `"path" 'alias import-as`;
- `'name [ ... ] def`;
- `'name private`.

Definitions are public by default. A private word remains available throughout
its package but cannot be called through an imported qualifier.

Toy collects declarations across the directory before any word body runs.
File names and file order therefore have no semantic effect. Duplicate
definitions, inconsistent package names, executable top-level code, and
dependency cycles are errors. Subdirectories are separate packages and are not
loaded automatically.

There is no package initialization hook. Initialization with observable effects
is an ordinary word called explicitly by the application.

## Exact Imports

An import string has one interpretation:

- a relative directory is resolved from the importing package;
- an absolute directory is used directly;
- `core:name` resolves beneath Toy's configured core directory.

There are no fallback directories, filename substitutions, package registries,
or environment search paths. A package is loaded once per interpreter state by
its canonical directory.

Without an alias, the declared package name becomes the qualifier:

```toy
"../math" import
21 math.double
```

`import-as` changes only the local qualifier:

```toy
"../math" 'm import-as
21 m.double
```

The dot is namespace qualification, never path traversal. Imports are local to
the importing package and are not re-exported transitively.

The installed CLI finds `core:` packages beside the SDK. A source build also
accepts `core` beside the executable, and `--core-path DIR` overrides that
location. An embedder sets `toy_state_config.core_package_path` explicitly.
Toy currently ships the Toy-written `core:json`, hybrid C/Toy `core:random`,
and native `core:ffi` packages. Core packages use the same import and namespace
rules as project packages. Their maintained word metadata is available at
runtime after import, so expressions such as `'random.shuffle doc` return the
same stack effects and descriptions used by editor hover.

## Standalone Evaluation

`--eval` and `--file` execute code in the host/root scope rather than turning a
file into a package:

```console
toy --eval "1 2 + print"
toy --file examples/factorial.toy
```

`--file` may be repeated; all files run in the same state. Arguments after the
last file are available through `argc` and `argv`. Root code may import
packages, with relative imports resolved from the process working directory.

## Packages Backed by C

A package can contain Toy definitions, a compiled C extension, or both. A
`toy.package` manifest names the shared library:

```ini
name = sample
extension = toy_sample.dll
```

The extension is an exact path relative to the package directory, or an
absolute path. The descriptor exported by the library, the manifest name, and
any source package declaration must agree. From Toy, the result imports exactly
like the source package above. See [Using C Libraries](./c-libraries.md) to
choose between dynamic FFI, a generated binding, and a handwritten extension,
and to build the corresponding artifacts.

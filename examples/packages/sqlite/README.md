# SQLite Interop Example

This Toy package wraps SQLite in a handwritten C extension. The adapter adds
row-state validation, tailored errors, and an idiomatic interface beyond direct
C calls. The focused
[`sqlite.json`](../bindgen/sqlite/sqlite.json) manifest shows how far the
generic generator can cover the same C API.

Work from a writable copy of this directory. The commands below assume it is
the current directory; replace `path/to/toy` and `path/to/sqlite` with the real
directories.

## Manual Build

Compile the extension directly. On Windows, using a SQLite library compatible
with the selected compiler:

```console
cc -Wall -Wextra -Wpedantic -shared toy_sqlite.c -I path/to/toy/include -I path/to/sqlite/include path/to/sqlite/lib/sqlite3.lib -o toy_sqlite.dll
```

On Linux, a typical system-library build is:

```console
cc -Wall -Wextra -Wpedantic -fPIC -shared toy_sqlite.c -I path/to/toy/include -lsqlite3 -o toy_sqlite.so
```

On macOS, use `-dynamiclib` and write `toy_sqlite.dylib`. Create
`toy.package` beside the compiled library, using its real filename:

```ini
name = sqlite
extension = toy_sqlite.dll
```

## Optional Helper

`toy-c-package` performs the same compiler, linker, and manifest steps:

```console
toy-c-package . toy_sqlite.c --include path/to/sqlite/include --lib path/to/sqlite/lib/sqlite3.lib
```

Using a static SQLite archive makes the Toy package self-contained. Using an
import/shared library means SQLite's runtime library must also be discoverable
by the operating system. Run the standalone demo with:

```console
toy --file demos/people.toy
```

The demo imports the parent package directory relative to its own source file;
no global installation or search path is involved.

`open` and `prepare` return typed Toy resources. The database uses
`sqlite3_close_v2` and statements use `sqlite3_finalize`, so final reference
release and state shutdown clean them up automatically. Words implemented by
the extension consume their inputs like ordinary Toy words; use `dup` when a
database or statement is needed again.

The package exposes a deliberately small slice:

- databases: `open`, `exec`, `changes`, and `last-insert-rowid`;
- statements: `prepare`, `step`, `reset`, and `clear-bindings`;
- parameters: `bind-int`, `bind-float`, `bind-text`, and `bind-null`;
- rows: `column-count`, `column-name`, `column-null?`, `column-int`,
  `column-float`, and `column-text`.

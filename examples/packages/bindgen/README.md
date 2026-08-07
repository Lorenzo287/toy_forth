# Generated Binding Examples

## Minimal C Binding

`clib/clib.json` describes `strlen` and `strcmp`. Generate the C wrapper with
the installed frontend:

```console
toy-bindgen --package clib clib/clib.json clib/generated.c
```

The frontend runs the generator shipped in the SDK. It can also be invoked
directly:

```console
node path/to/toy/share/toy/bindgen/generate-binding.js --package clib clib/clib.json clib/generated.c
```

Compile the generated C without linking the Toy runtime:

```console
cc -Wall -Wextra -Wpedantic -shared clib/generated.c -I path/to/toy/include -o clib/toy_clib.dll
```

Create `clib/toy.package`:

```ini
name = clib
extension = toy_clib.dll
```

On Linux, add `-fPIC` and use `toy_clib.so`; on macOS use `-dynamiclib` and
`toy_clib.dylib`. Run the result with:

```console
toy --file demos/clib.toy
```

As an optional shortcut, `toy-c-package clib clib/generated.c` performs the
compiler and manifest steps.

## Owned Resource

`stdio/stdio.json` wraps `FILE *` as an owned `stdio.file` resource. Dropping
the final Toy reference calls `fclose` automatically:

```console
toy-bindgen --package stdio stdio/stdio.json stdio/generated.c
cc -Wall -Wextra -Wpedantic -shared stdio/generated.c -I path/to/toy/include -o stdio/toy_stdio.dll
```

Create `stdio/toy.package`:

```ini
name = stdio
extension = toy_stdio.dll
```

Then run it. `toy-c-package stdio stdio/generated.c` is the optional helper for
the compiler and manifest steps.

```console
toy --file demos/stdio.toy -- demos/stdio.toy
```

These standard-C functions need no additional library configuration. See
[`docs/bindgen.md`](../../../docs/bindgen.md) for the complete manifest
contract.

## SQLite Binding

`sqlite/sqlite.json` demonstrates hidden C arguments, resource-specific
errors, binary-safe pointer-length strings, and a statement that retains its
database:

```console
toy-bindgen --package sqlite sqlite/sqlite.json sqlite/generated.c
cc -Wall -Wextra -Wpedantic -shared sqlite/generated.c -I path/to/toy/include -I path/to/sqlite/include path/to/sqlite/lib/sqlite3.lib -o sqlite/toy_sqlite.dll
```

Create `sqlite/toy.package`:

```ini
name = sqlite
extension = toy_sqlite.dll
```

On Linux use `-fPIC -shared`, `-lsqlite3`, and `toy_sqlite.so`. On macOS use
`-dynamiclib` and `toy_sqlite.dylib`. In both cases, put the resulting filename
in `toy.package`.

The helper equivalent is:

```console
toy-c-package sqlite sqlite/generated.c --include path/to/sqlite/include --lib path/to/sqlite/lib/sqlite3.lib
toy --file demos/sqlite.toy
```

The compiler must match the SQLite library's ABI. The separate
[`examples/packages/sqlite/`](../sqlite/) package is handwritten to show custom
row-state validation and a more idiomatic Toy interface.

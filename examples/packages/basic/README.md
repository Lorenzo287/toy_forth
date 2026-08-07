# Basic C Extension Example

This is the smallest complete Toy package with a C extension. It has no
foreign-library dependency: `toy_basic.c` exports one `twice` word.

## Manual Build

Compile the C file as a shared library. It includes only the SDK's standalone
`toy.h`; do not link `toy_runtime`:

```console
cc -Wall -Wextra -Wpedantic -shared toy_basic.c -I path/to/toy/include -o toy_basic.dll
```

On Linux, add `-fPIC` and use `toy_basic.so`; on macOS replace `-shared` with
`-dynamiclib` and use `toy_basic.dylib`.

Create `toy.package` beside the library, using the filename for your platform:

```ini
name = basic
extension = toy_basic.dll
```

Then run the demo:

```console
toy --file demo/main.toy
```

## Optional Helper

`toy-c-package` builds the same extension and manifest, printing the compiler
commands it runs:

```console
toy-c-package . toy_basic.c
toy --file demo/main.toy
```

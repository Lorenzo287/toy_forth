# Using C Libraries

Toy can call existing C libraries in three ways. The right one depends less on
the size of the library than on the shape of the part you want to expose.

| Route | Best for | Tradeoff |
| --- | --- | --- |
| Dynamic FFI | Trying a few scalar or string functions immediately | No compiler step, but signatures are unchecked promises |
| Generated binding | Regular APIs with many similar functions | Repeatable wrappers, within the generator's type and ownership model |
| Handwritten C extension | Handles, custom lifetimes, structs, callbacks, or a deliberately small interface | A little C code, with complete control over the boundary |

If a C application should own the main loop and use Toy for scripts, use
[embedding](./embedding.md) instead. For example, a C/Raylib game can keep
windowing and rendering in C while calling Toy words for game rules. If Toy
should own the program, expose the useful part of Raylib through one of the
routes below.

## Dynamic FFI

The `core:ffi` package opens a shared library, binds a symbol to a fixed
signature, and calls it without compiling a wrapper:

```toy
"core:ffi" 'c import-as

"hello"
"msvcrt.dll" c.open
"strlen" "cstr -> usize" c.bind
c.call print
```

The library name is platform-specific. Common C runtime names include
`libc.so.6` on Linux and `/usr/lib/libSystem.B.dylib` on macOS. The runnable
[`examples/ffi.toy`](../examples/ffi.toy) accepts that name as its
first argument.

Release SDKs include the compiled `core:ffi` package. A platform may also need
the libffi shared library available to its operating-system loader. Building
Toy from source requires the matching libffi headers and link library.

An FFI signature lists its argument types in call order, followed by `->` and
its return type:

```text
i32 i32 -> i32
cstr -> usize
-> cstr
i32 -> void
```

The supported types are `bool`, signed and unsigned integers from 8 to 64 bits,
`isize`, `usize`, `f32`, `f64`, `cstr`, and `void` as a return type. Integers are
range-checked. Toy strings passed as `cstr` are copied and cannot contain NUL;
a returned `cstr` is treated as borrowed and copied immediately.

FFI is a good exploratory boundary, not a general C interface. It cannot pass
raw pointers, structs, arrays, output parameters, callbacks, owned strings, or
variadic functions. The signature must match the C declaration exactly:
libffi cannot detect a mistake, and a wrong signature can corrupt the process.
Open only trusted libraries.

## Compiled C Packages

Generated and handwritten bindings both become ordinary Toy packages. A
compiled package directory contains a shared library and a small manifest:

```text
vendor/
`-- sample/
    |-- toy_sample.dll
    `-- toy.package
```

```ini
name = sample
extension = toy_sample.dll
```

Use `.so` on Linux and `.dylib` on macOS. The package can also contain `.toy`
files with the same package name. Import it by its exact directory, just like a
source package:

```toy
"../vendor/sample" 's import-as
21 s.twice print
```

The shared library includes the standalone `toy.h` header and does not link the
Toy runtime. `toy-c-package` performs the normal compile and manifest steps:

```console
toy-c-package vendor/sample vendor/sample/toy_sample.c
```

It accepts `--cc`, `--include`, `--lib-dir`, `--lib`, and lower-level compiler
or linker flags when the foreign library needs them. The equivalent manual
build is simply a shared-library compilation followed by writing the manifest:

```console
cc -Wall -Wextra -Wpedantic -shared vendor/sample/toy_sample.c -I path/to/toy/include -o vendor/sample/toy_sample.dll
```

On Linux add `-fPIC`; on macOS use `-dynamiclib`. Static foreign libraries are
linked into the extension. Shared foreign libraries must still be discoverable
by the operating-system loader when Toy imports the package. On Windows, Toy
searches the extension's directory first for its dependent DLLs, so a package
can carry them beside the extension named by `toy.package`. Header paths only
provide declarations; they do not link the library.

## Generated Bindings

`toy-bindgen` reads an explicit JSON description and writes a C extension. A
small manifest can expose several ordinary C functions:

```json
{
  "package": "clib",
  "headers": ["string.h"],
  "functions": [
    {
      "name": "strlen",
      "returns": "usize",
      "args": ["cstr"]
    },
    {
      "name": "strcmp",
      "word": "compare",
      "returns": "i32",
      "args": ["cstr", "cstr"]
    }
  ]
}
```

Generate and compile it with the installed tools:

```console
toy-bindgen --package clib vendor/clib/clib.json vendor/clib/generated.c
toy-c-package vendor/clib vendor/clib/generated.c
```

The generated wrapper checks Toy values, performs the direct C calls, and
exposes the result as qualified words. Toy code does not use `ffi.open` or
signatures:

```toy
"../vendor/clib" 'c import-as
"Toy" c.strlen print
"alpha" "beta" c.compare print
```

The generator supports scalars, C strings, adjacent pointer-and-length byte
inputs, copied byte results, constants, status codes, owned opaque resources,
output resources, and simple dependent lifetimes. It intentionally does not
guess ownership from a C header. The
[binding manifest reference](./bindgen.md) documents those forms when a simple
function list is no longer enough.

Use generated bindings when an API is regular and its ownership fits those
rules. The examples cover standard C functions and a resource-oriented SQLite
subset under [`examples/packages/bindgen/`](../examples/packages/bindgen/).

## Handwritten C Extensions

A handwritten extension is often the clearest choice when the desired Toy API
is much smaller than the C library. It is also the escape hatch for custom
conversion, validation, state, and ownership.

```c
#define TOY_EXTENSION_IMPLEMENTATION
#include "toy.h"

static toy_status twice(toy_state *state) {
    int64_t value = 0;
    if (!toy_get_int(state, 0, &value)) {
        return toy_fail(state, "sample.twice expected an integer");
    }
    if (!toy_pop(state, 1)) {
        return toy_fail(state, "sample.twice could not consume its input");
    }
    return toy_push_int(state, value * 2);
}

static const toy_native_word words[] = {
    {
        .name = "twice",
        .callback = twice,
        .stack_effect = "n -- 2n",
        .description = "Double an integer.",
    },
};

static const toy_extension extension = {
    sizeof(toy_extension),
    "sample",
    words,
    sizeof(words) / sizeof(words[0]),
};

TOY_EXTENSION_EXPORT const toy_extension *toy_extension_init(
    uint32_t abi_version, const toy_extension_api *api) {
    if (!toy_extension_bind(abi_version, api)) return NULL;
    return &extension;
}
```

Define `TOY_EXTENSION_IMPLEMENTATION` in exactly one C file. Native words read
their inputs by stack depth, consume them explicitly, and push their results.
`toy_fail()` stores a diagnostic and returns `TOY_ERROR`. The optional stack
effect and description are copied during package registration and exposed by
Toy's `doc` word. Official `core:` package metadata also supplies this
documentation to the LSP; arbitrary extension binaries are not loaded by the
language server.

External handles can be wrapped with `toy_push_resource()`. A resource has an
exact type name and an optional destructor that runs once when the last Toy
reference disappears. This makes handles usable in ordinary collections
without exposing pointers to Toy code. Borrowed pointers returned by
`toy_get_resource()` remain valid only while their resource value is alive.

The dependency-free [`basic`](../examples/packages/basic/) package is the
smallest complete extension. The handwritten
[`Raylib`](../examples/packages/raylib/) and
[`SQLite`](../examples/packages/sqlite/) packages show typed resources and the
small policies that real libraries usually need.

## Choosing for a Real Project

For a small Raylib experiment:

- embed Toy when C should own the window, event loop, and rendering;
- write a narrow extension when Toy should own the game loop;
- generate wrappers for regular scalar calls, then add handwritten words only
  where structures or ownership require them;
- use FFI for isolated calls while exploring, not as a complete Raylib binding.

The same progression works for most libraries: try the smallest boundary that
expresses the required types, then move to C only when the library's real
ownership model demands it. All three routes call native code in the Toy
process and therefore trust that code like any other linked library.

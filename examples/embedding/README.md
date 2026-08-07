# Embedding Examples

These are ordinary C applications that link the static runtime shipped in the
Toy SDK.

- `embed.c` registers a C word, evaluates Toy code, and calls a Toy word.
- `callbacks.c` captures normal output and diagnostics separately.
- `values.c` exchanges collections and a retained quotation across the API.

## Windows

Use the compiler ABI that matches the SDK archive. A GCC-style SDK can build
each host directly:

```console
gcc embed.c -I path/to/toy/include path/to/toy/lib/libtoy_runtime.a -luser32 -o embed.exe
gcc callbacks.c -I path/to/toy/include path/to/toy/lib/libtoy_runtime.a -luser32 -o callbacks.exe
gcc values.c -I path/to/toy/include path/to/toy/lib/libtoy_runtime.a -luser32 -o values.exe
```

For an MSVC SDK, use its matching `.lib` archive and normal MSVC compiler and
linker flags instead.

## Linux and macOS

Use the archive from the same SDK with its compatible compiler. Linux also
links `dl`; macOS does not:

```console
cc embed.c -I path/to/toy/include path/to/toy/lib/libtoy_runtime.a -lm -ldl -o embed
cc callbacks.c -I path/to/toy/include path/to/toy/lib/libtoy_runtime.a -lm -ldl -o callbacks
cc values.c -I path/to/toy/include path/to/toy/lib/libtoy_runtime.a -lm -ldl -o values
```

On macOS, omit `-ldl`. The commands deliberately use only the public
`toy.h` header and static archive; they are a useful starting point for a
Makefile, CMake project, or another build system.

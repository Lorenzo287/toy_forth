# Dynamic FFI Example

This package imports `core:ffi`, opens the C runtime passed as its first
argument, and calls `strlen`. Run it directly from the SDK without changing the
SDK. Replace `path/to/toy` with its directory:

```console
toy --file path/to/toy/examples/ffi/main.toy -- msvcrt.dll
```

Use the appropriate C runtime library name on other platforms. See
[`docs/c-libraries.md`](../../docs/c-libraries.md#dynamic-ffi) for source-build
dependencies, supported signatures, and safety constraints.

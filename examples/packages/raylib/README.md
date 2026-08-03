# Raylib Interop Example

This Toy package exposes Raylib through a handwritten C extension. The adapter
translates by-value structs and packed colors, coordinates window/GPU
lifetimes, and wraps `Texture2D` as an owned `raylib.texture` resource.

Work from a writable copy of this directory. The commands below assume it is
the current directory; replace `path/to/toy` and `path/to/raylib` with the real
directories.

## Manual Build

After installing a Raylib build compatible with your compiler, compile the
extension directly. A typical Windows command is:

```console
cc -Wall -Wextra -Wpedantic -shared toy_raylib.c -I path/to/toy/include -I path/to/raylib/include path/to/raylib/lib/raylib.lib -lopengl32 -lgdi32 -lwinmm -lshell32 -o toy_raylib.dll
```

Create `toy.package` beside the compiled library:

```ini
name = raylib
extension = toy_raylib.dll
```

On Linux use `-fPIC -shared` and `toy_raylib.so`; on macOS use `-dynamiclib`
and `toy_raylib.dylib`. Link the Raylib and system libraries listed by the
Raylib distribution for that platform.

## Optional Helper

`toy-c-package` performs the same compiler, linker, and manifest steps:

```console
toy-c-package . toy_raylib.c --include path/to/raylib/include --lib path/to/raylib/lib/raylib.lib --lib opengl32 --lib gdi32 --lib winmm --lib shell32
```

Run either standalone demo, passing the package directory first:

```console
toy --file demos/shapes.toy -- .
toy --file demos/texture.toy -- . path/to/image.png
```

Both demos import the exact package directory supplied on the command line.

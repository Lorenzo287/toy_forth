# Build Instructions for Toy Forth

## Environments

Builds are configured using default cmake profiles `-DCMAKE_BUILD_TYPE=<Profile>`
and with custom build modes `-DBUILD_MODE=<Mode>`.

You can use the "Unix Makefiles" Generator (defaults to MinGW on Windows) if you don't have "Ninja" installed.

It's possible to override the default compiler using `-DCMAKE_C_COMPILER=<Compiler>`.

### 1. Release (Optimized)

Optimized build for production usage (works on Windows/Linux).

```bash
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

### 2. LeakCheck

Development build for tracking leaks.

- **Windows (MSVC/MinGW)**: Defines `STB_LEAKCHECK` (ensure `stb_leakcheck_dumpmem()` is called in main).
- **Linux/WSL**: Uses AddressSanitizer.

```bash
cmake -S . -B build-leak -G "Unix Makefiles" -DBUILD_MODE=LeakCheck
cmake --build build-leak
```

### 3. Profile

Development build for profiling symbols (uses `-O2`).

_Note: On Windows use MSVC or Clang, MinGW is not supported for this mode._

```bash
cmake -S . -B build-prof -G "Ninja" -DBUILD_MODE=Profile -DCMAKE_C_COMPILER=clang
cmake --build build-prof
cd build-prof
samply record toy_forth.exe ../fth/test_prof.fth
```

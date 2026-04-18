# Build Instructions for Toy Forth

## Environments

All builds are configured using `-DCMAKE_BUILD_TYPE=<Profile>`.

It's possible to override the default compiler using `-DCMAKE_C_COMPILER=<Compiler>`.

Can use "Unix Makefiles" if you don't have "Ninja" installed, on Windows it selects MinGW

### 1. Release (Optimized)

Optimized build for production usage (works on Windows/Linux).

```bash
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

### 2. LeakCheck

Development build for tracking leaks.

- **Windows (MSVC/MinGW)**: Defines `STB_LEAKCHECK` (ensure `stb_leakcheck_dumpmem()` is called in main).
- **Linux/WSL**: Uses `AddressSanitizer` (ASan).

```bash
cmake -S . -B build-leak -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=LeakCheck
cmake --build build-leak
```

### 3. Profile

Development build for profiling symbols (uses `-O2`).

_Note: On Windows, use MSVC or Clang. GCC/MinGW is not supported for this mode._

```bash
cmake -S . -B build-prof -G "Ninja" -DCMAKE_BUILD_TYPE=Profile -DCMAKE_C_COMPILER=clang-cl
cmake --build build-prof
cd build-prof
samply record toy_forth.exe ../fth/test_prof.fth
```

# Build Instructions for Toy Forth

## Environments

### 1. Windows Optimized (Release)
Optimized build for production usage.
```bash
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

### 2. Windows LeakCheck (stb_leakcheck)
Development build for tracking leaks on Windows.
```bash
cmake -S . -B build-leak -G "Ninja" -DENABLE_LEAKCHECK=ON
cmake --build build-leak
```
*Note: Ensure `stb_leakcheck_dumpmem()` is called in `main.c` before exit.*

### 3. Linux/WSL LeakCheck (ASan)
Development build for tracking memory errors and leaks using AddressSanitizer.
```bash
# Inside Linux/WSL environment
cmake -S . -B build-linux-asan -G "Unix Makefiles" -DENABLE_ASAN=ON
cmake --build build-linux-asan
```

# Toy Forth Interpreter

A minimalist, stack-based Forth-like interpreter written in C. This project is a toy implementation designed to demonstrate basic tokenization, object management with reference counting, and stack-oriented execution.

## Features

- **Stack-based architecture**: All operations perform their logic on a central data stack.
- **Dynamic Object System**: Support for multiple types including:
  - Integers (`TF_OBJ_TYPE_INT`)
  - Floating point numbers (`TF_OBJ_TYPE_FLOAT`)
  - Booleans (`TF_OBJ_TYPE_BOOL`)
  - Strings (`TF_OBJ_TYPE_STR`)
  - Symbols (`TF_OBJ_TYPE_SYMBOL`)
  - Lists (`TF_OBJ_TYPE_LIST`)
- **Memory Management**: Simple reference counting for objects (`retain_obj`/`release_obj`).
- **Core Operations**: Basic integer arithmetic: `+`, `-`, `*`, `/`.

## Getting Started

The project uses **CMake** as its unified build system, supporting both Windows (MSVC/MinGW) and Linux/WSL (GCC/Clang).

### Prerequisites

- A C compiler (GCC, Clang, or MSVC)
- CMake (version 3.10 or higher)

### Building the Project

To avoid artifact corruption when switching between Windows and WSL, it is recommended to use separate build directories.

#### For Windows
1. Open your terminal (PowerShell or Command Prompt).
2. Create a Windows-specific build directory:
   ```powershell
   mkdir build
   cd build
   ```
3. Generate and build:
   ```powershell
   cmake ..
   cmake --build .
   ```

#### For Linux / WSL (with Debug symbols for Valgrind)
1. Open your WSL terminal.
2. Create a Linux-specific build directory:
   ```bash
   mkdir build-linux
   cd build-linux
   ```
3. Generate for Debug mode and build:
   ```bash
   cmake -DCMAKE_BUILD_TYPE=Debug ..
   make
   ```

## Usage

Run Forth programs by passing the file path to the compiled executable.

**On Windows:**
```powershell
.\build\toy_forth.exe program.fth
```

**On Linux / WSL:**
```bash
./build-linux/toy_forth program.fth
```

### Example

The provided `program.fth`:
```forth
5 10 + 1 + 2 /
```
The interpreter will push `5` and `10`, add them (`15`), push `1`, add it (`16`), push `2`, and divide (`8`).

## Architecture

- **`tf_obj`**: The base object structure that holds values and metadata.
- **`tf_lexer`**: A simple tokenizer that parses text into a list of `tf_obj`.
- **`tf_ctx`**: Execution context containing the data stack and function table.
- **`tf_exec`**: The execution engine that processes tokens and calls functions.
- **`tf_lib`**: Core library functions (e.g., math operations).

## Future Improvements

- [ ] Support for user-defined functions (colon definitions).
- [ ] Better type checking in math operations.
- [ ] Support for variables and constants.
- [ ] Additional stack manipulation words (`DUP`, `DROP`, `SWAP`, etc.).
- [ ] Quoted expressions for functional-like behavior.

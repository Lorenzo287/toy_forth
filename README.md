# Toy Forth Interpreter

A minimalist, stack-based Forth-like interpreter written in C, based on the **[Toy Forth](https://github.com/antirez/toyforth)** project by **Salvatore Sanfilippo (antirez)**.

## Features

- **Stack-based architecture**: All operations perform their logic on a central data stack.
- **Dynamic Object System**: Support for Integers, Floats, Booleans, Strings, Symbols, and Lists.
- **Memory Management**: Automatic reference counting for all objects.
- **Core Operations**: Basic arithmetic and stack manipulation.

## Getting Started

The project uses CMake as its unified build system. To avoid artifact corruption between Windows and WSL, separate build directories are recommended.

### Building for Windows
```powershell
mkdir build; cd build
cmake ..
cmake --build .
```

### Building for Linux / WSL (with Debug symbols)
```bash
mkdir build-linux; cd build-linux
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

## Usage

Run Forth programs by passing the file path:
```bash
# Windows
.\build\toy_forth.exe fth\program.fth

# Linux / WSL
./build-linux/toy_forth fth/program.fth
```

### Example (`fth/program.fth`)
```forth
5 10 + 1 + 2 /
```
The interpreter will push `5` and `10`, add them (`15`), push `1`, add it (`16`), push `2`, and divide, leaving `8` on the stack.

## Architecture

- **Lexer**: Tokenizes source text into a list of objects.
- **Context**: Maintains the data stack and function dictionary.
- **Execution Engine**: Processes tokens, resolving symbols and executing functions.
- **Core Library**: Implements primitive words (math, stack ops).

## Future Improvements (Roadmap)

### Phase 1: Core Functionality
- [x] **Standard Forth Words**: Implement essential stack manipulation words: `dup`, `drop`, `swap`, `over`, `rot`.
- [ ] **I/O & Control**: Added `print`. Still need basic conditional/looping constructs (`if/else`, `while`).
- [x] **Architectural Cleanup**: Refactored the object system and stack wrappers for better type-safety and consistency.

### Phase 2: Advanced Features (Antirez Proposals)
- [ ] **Variable Capturing**: Implement the `(a b)` syntax to capture stack values into local variables, and `$a`, `$b` to retrieve them.
- [ ] **Quoted Symbols**: Support `'symbol` syntax to push a symbol to the stack without immediate execution.
- [ ] **Quotations (Blocks)**: Support `[ ... ]` syntax to push a block of code (list of tokens) to the stack.
- [ ] **User-Defined Functions**: Implement colon definitions `: name ... ;` to register new words in the dictionary.
- [ ] **Block Execution**: Implement `exec` or similar to run a block of code pushed to the stack.

### Phase 3: System Enhancements
- [ ] **Dictionary Management**: Improve the function table for faster lookups (e.g., hash table).
- [ ] **Memory Management**: Refine reference counting to handle potential cycles if recursive lists/blocks are added.
- [ ] **Variables & Constants**: Support for global variables and constants.

## License

This project is licensed under the **MIT License** (see the [LICENSE](LICENSE) file for details).


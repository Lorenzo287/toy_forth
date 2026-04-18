# Toy Forth Interpreter

A minimalist, stack-based Forth-like interpreter written in C, based on the **[Toy Forth](https://github.com/antirez/toyforth)** project by **Salvatore Sanfilippo (antirez)**.

## Features

- **Stack-based architecture**: All operations perform their logic on a central data stack.
- **Dynamic Object System**: Support for Integers, Floats, Booleans, Strings, Symbols, and Lists.
- **Floating Point Support**: Math operations support mixed-type arithmetic with automatic type promotion (INT + FLOAT = FLOAT).
- **Memory Management**: Automatic reference counting for all objects.
- **Core Operations**: Basic arithmetic, comparison, and stack manipulation.

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

# Enable debug mode (prints tokenized program and final stack)
.\build\toy_forth.exe --debug fth\program.fth
```

### Example (`fth/program.fth`)
```forth
\ Single line comment
( Multi-line or inline
  comment style )

: square dup * ;
5 square println
6 square . "\n" . ( . Alias for print )

\ Functional style definition
'cube [ dup square * ] def
3 cube .s ( Show stack )
```

## Architecture

- **Lexer**: Tokenizes source text into a list of objects. Supports `\` and `(...)` comments.
- **Context**: Maintains the data stack and a dictionary of "Native" (C-based) and "User" (Forth-based) functions.
- **Execution Engine**: Processes tokens, resolving symbols and executing functions using a tagged-union dispatch system.
- **Core Library**: Implements primitive words:
    - **Math**: `+`, `-`, `*`, `/`, `%`, `mod`, `abs`, `max`, `min`.
    - **Stack**: `dup`, `drop`, `swap`, `over`, `rot`.
    - **I/O**: `println`, `print`, `.`, `.s`.
    - **Logic**: `==`, `!=`, `<`, `>`, `<=`, `>=`.
    - **Control**: `if`, `ifelse`, `while`, `exec`.

## Future Improvements (Roadmap)

### Phase 1: Core Functionality (Completed)
- [x] **Standard Forth Words**: Implement essential stack manipulation words: `dup`, `drop`, `swap`, `over`, `rot`.
- [x] **Solid Math Core**: Added support for Floating Point numbers, type promotion, and safety checks (division by zero).
- [x] **I/O & Control**: Added `print`, `if`, `ifelse`, `while` (block-based) and more. Supports direct boolean conditions.
- [x] **Debug Mode**: Added `--debug` flag for inspection.

### Phase 2: Advanced Features (Antirez Proposals) (In Progress)
- [ ] **Variable Capturing**: Implement the `(a b)` syntax to capture stack values into local variables, and `$a`, `$b` to retrieve them.
- [x] **Quoted Symbols**: Support `'symbol` syntax to push a symbol to the stack without immediate execution.
- [x] **Quotations (Blocks)**: Support `[ ... ]` syntax to push a block of code (list of tokens) to the stack.
- [x] **User-Defined Functions**: Implement colon definitions `: name ... ;` and functional `def`.
- [x] **Block Execution**: Implement `exec` or similar to run a block of code pushed to the stack.

### Phase 3: Architectural Refactoring (In Progress)
- [ ] **Iterative Interpreter & Return Stack**: Move away from C recursion. Implement an explicit return stack of `(program, ip)` frames to make the interpreter fully iterative and safer.
- [x] **Hash Table Dictionary**: Replace the linear scan in `get_func` with a hash table (e.g., djb2 hash with linear probing) for O(1) word lookups.
- [x] **Refined Native Callbacks**: Simplify the native function interface by removing the `name` parameter and providing dedicated C functions for each Forth word, eliminating redundant internal dispatch.

### Phase 4: System Enhancements
- [ ] **Memory Management**: Refine reference counting to handle potential cycles if recursive lists/blocks are added.
- [ ] **Variables & Constants**: Support for global variables and constants.

## License

This project is licensed under the **MIT License** (see the [LICENSE](LICENSE) file for details).


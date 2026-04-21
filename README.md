# Toy Forth

A minimalist, stack-based interpreter written in C. While it follows the Forth tradition of a data stack and a dictionary of words, it incorporates modern "high-level" features like a dynamic object system, quotations, and automatic memory management.

Based on the original [Toy Forth](https://github.com/antirez/toyforth) project by **Salvatore Sanfilippo (antirez)**.

## Key Features

### Language Features

- **Dynamic Object System**: Native support for **Integers, Floats, Booleans, Strings, Symbols,** and **Lists**.
- **Quotations & Blocks**: First-class code blocks `[ ... ]` and quoted symbols `'symb` allow for deferred execution.
- **Variable Capturing**: Named local variables with dynamic scoping using `{ a b }` and `$a` syntax.
- **First-class Control Flow**: Branches (`if`) and loops (`while`, `each`) are simple words that consume code blocks from the stack.

### Engine & Performance

- **Iterative Execution**: An explicit return stack of frames eliminates C recursion for user-defined words, preventing stack overflows.
- **Automatic Memory Management**: A uniform reference counting model (`retain_obj`/`release_obj`) handles all heap-allocated objects.
- **$O(1)$ Word Lookup**: A high-performance hash table dictionary ensures fast dispatch.
- **Type Promotion**: Automatic mixed-type arithmetic (e.g., `1 2.5 +` -> `3.5`).

## Showcase

### Definitions & Variables

Define new words using the classic colon syntax or by binding blocks to symbols. Use variable capturing to avoid complex stack manipulation:

```forth
\ Classic colon definition with local variables
: square ( n -- n*n ) {n} $n $n * ;

\ Functional style definition using 'def'
'cube [ {n} $n square $n * ] def

5 square print  \ 25
3 cube .        \ 27

\ Captured variables are visible to inner blocks (Dynamic Scoping)
10 {x}
[ $x 5 + . ] exec \ 15
```

### Deferred Execution (Quotations)

Code is data. You can defer execution by "quoting" a symbol or wrapping code in a block:

```forth
'dup          \ Pushes the symbol 'dup' to the stack instead of running it
[ 1 2 + ]     \ Pushes a list containing 1, 2, and +
exec          \ Now execute the block on the stack -> 3
```

### Iteration & Control Flow

Blocks allow for concise and expressive loops. For conditional logic (`if` and `ifelse`), the interpreter makes no distinction between a value and a block that produces a value, allowing for immediate or lazy evaluation:

```forth
\ Option 1: Immediate Boolean (calculated BEFORE 'if')
1 2 < [ "True!" print ] if

\ Option 2: Deferred Block (calculated BY 'if')
[ 1 2 < ] [ "True!" print ] if

\ Execute a block 5 times
5 [ "Hello! " printf ] times

\ Iterate over a list
[ 1 2 3 4 5 ] [ printf " " printf ] each

\ While loop: [ condition ] [ body ] while
10 [ dup 0 > ] [ dup printf " " printf 1 - ] while
```

## System & Utility Words

Beyond basic stack operations, Toy Forth provides utilities for data manipulation and interaction:

- **List access**: `geth` and `seth` allow for $O(1)$ indexed access to lists.
- **System interaction**: `rand` for randomness, `sleep` for pausing, `time` for clock access, and `exit` for termination.

_Example: Updating a list_

```forth
[ 1 2 3 ] {list}
$list 0 rand 100 % seth  \ Sets index 0 of $list to a random number
$list print
```

## Standard Library

Toy Forth includes a robust set of built-in words:

| Category          | Words                                                      |
| ----------------- | ---------------------------------------------------------- |
| **Stack**         | `dup`, `drop`, `swap`, `over`, `rot`                       |
| **Math**          | `+`, `-`, `*`, `/`, `%`, `mod`, `abs`, `neg`, `max`, `min` |
| **Comparison**    | `==`, `!=`, `<`, `>`, `<=`, `>=`                           |
| **Logic/Control** | `if`, `ifelse`, `while`, `times`, `each`, `exec`           |
| **I/O**           | `print`, `printf`, `.`, `.s` (show stack), `key`, `input`  |
| **System/Utils**  | `geth`, `seth`, `len`, `rand`, `sleep`, `time`, `exit`     |
| **Definition**    | `:`, `def`                                                 |

---

## Architecture

- **Lexer**: A recursive-descent tokenizer that supports nested blocks, strings, quoted symbols, and multiple comment styles (`\` and `(...)`).
- **Engine**: An iterative execution engine using a frame-based call stack. This hybrid approach ensures that user-defined word recursion is safe from C stack limits.
- **Context**: Maintains the data stack, the global function hash table, and the active execution frames.
- **Memory**: Every object is a tagged union with an internal reference count. The system is designed to be leak-free (verifiable with `stb_leakcheck`).

## Getting Started

### Build (see [BUILD](BUILD.md))

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Usage

```bash
# Basic run
./toy_forth fth/program.fth

# Debug mode (prints tokenized program and final stack state)
./toy_forth --debug fth/program.fth
```

## Roadmap

- [x] **Stack Ops**: `dup`, `drop`, `swap`, `over`, `rot`.
- [x] **Math Core**: Floating point support with type promotion.
- [x] **Quotations**: First-class block `[ ... ]` and quoted symbol `'symb` support.
- [x] **Word Definitions**: Both `: name ... ;` and `'name [ ... ] def`.
- [x] **Control Flow**: `if`, `ifelse`, `while`, `times`, `each`.
- [x] **Hash Table Dictionary**: $O(1)$ word lookups.
- [x] **Refcount System**: Automatic memory management.
- [x] **Iterative Execution**: Explicit return stack of frames.
- [x] **Variable Capturing**: Syntax for local variable binding `{a b} ... $a $b`.

## License

MIT

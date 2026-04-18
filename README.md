# Toy Forth

A minimalist, stack-based interpreter written in C. While it follows the Forth tradition of a data stack and a dictionary of words, it incorporates modern "high-level" features like a dynamic object system, quotations, and automatic memory management.

Based on the original [Toy Forth](https://github.com/antirez/toyforth) project by **Salvatore Sanfilippo (antirez)**.

## Key Features

- **Dynamic Object System**: Every item on the stack is a `tf_obj`. The interpreter natively supports **Integers, Floats, Booleans, Strings, Symbols,** and **Lists**.
- **Quotations & Blocks**: Code is data. Use `[ ... ]` to push a block of tokens, or `'symbol` to push a symbol to the stack without immediately executing it.
- **Polymorphic Control Flow**: Words like `if`, `ifelse`, `while`, `times`, and `each` consume blocks from the stack, enabling both traditional and functional logic.
- **Automatic Memory Management**: A uniform reference counting model (`retain_obj`/`release_obj`) handles all heap-allocated objects, including strings, symbols, and nested lists.
- **Type Promotion**: Mixed-type arithmetic is handled automatically (e.g., `1 2.5 +` results in a Float `3.5`).
- **$O(1)$ Word Lookup**: A hash table dictionary (djb2 hash with linear probing) ensures fast dispatch for both native C callbacks and user-defined words.

## Showcase

### Deferred Execution (Quoted Symbols & Blocks)

You can defer execution by "quoting" a symbol or wrapping code in a block:

```forth
'dup          \ Pushes the symbol 'dup' to the stack instead of running it
[ 1 2 + ]     \ Pushes a list containing 1, 2, and +
exec          \ Now execute the block on the stack -> 3
```

### Definitions

Define new words using the classic colon syntax or by binding blocks to symbols:

```forth
\ Classic colon definition
: square dup * ;

\ Functional style definition
'cube [ dup square * ] def

5 square println  \ 25
3 cube println    \ 27
```

### Iteration

Blocks allow for concise and expressive loops:

```forth
\ Execute a block 5 times
5 [ "Hello! " print ] times

\ Iterate over a list
[ 1 2 3 4 5 ] [ . " " . ] each

\ While loop: [ condition ] [ body ] while
10 [ dup 0 > ] [ dup . " " . 1 - ] while
```

### Flexible Conditions

For conditional logic (`if` and `ifelse`), the interpreter makes no distinction between a value and a block that produces a value, allowing for immediate or lazy evaluation:

```forth
\ Option 1: Immediate Boolean (calculated BEFORE 'if')
1 2 < [ "True!" println ] if

\ Option 2: Deferred Block (calculated BY 'if')
[ 1 2 < ] [ "True!" println ] if
```

## Standard Library

Toy Forth comes with a set of built-in words:

| Category | Words |
| --- | --- |
| **Stack** | `dup`, `drop`, `swap`, `over`, `rot` |
| **Math** | `+`, `-`, `*`, `/`, `%`, `mod`, `abs`, `max`, `min` |
| **Comparison** | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| **Logic/Control** | `if`, `ifelse`, `while`, `times`, `each`, `exec` |
| **I/O** | `print`, `println`, `.`, `.s` (show stack) |
| **Definition** | `:`, `def` |

## Architecture

- **The Lexer**: A recursive-descent tokenizer that handles nested blocks, strings, quoted symbols, and different comment styles.
- **The Context (`tf_ctx`)**: Maintains the data stack and the global function table.
- **The Engine**: An iterative execution engine that uses an explicit call stack for user-defined words to prevent C stack overflows, while utilizing a pragmatic hybrid approach for native control flow words.
- **Memory**: Every object is a tagged union with an internal reference count. The system is designed to be leak-free (verifiable with `stb_leakcheck`).

## Getting Started

### Build

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
- [x] **Iterative Execution**: Moving to an explicit return stack of frames to eliminate C recursion.
- [ ] **Variable Capturing**: Syntax for local variable binding `(a b) ... $a $b`.

## License

MIT

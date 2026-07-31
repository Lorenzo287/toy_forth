# Toy

Toy is a minimalist, stack-based language and runtime written in C. It began
with a traditional Forth shape, but has grown into a quotation-first
concatenative language inspired by
[Joy](<https://en.wikipedia.org/wiki/Joy_(programming_language)>).

The core idea is that **code is data**: values go onto the stack, then words transform
them, and most importantly programs are values too. From this Toy gets
higher-order words, control flow, and recursion without a separate layer of
special syntax, but simply by **concatenation**.

Toy began as an extension of Salvatore Sanfilippo's
[toyforth](https://github.com/antirez/toyforth), written for his C programming
course. Only recently I found Toy's close relative: [Aocla](https://github.com/antirez/aocla).
The variable capture and quoting ideas antirez briefly mentioned in the course seem to come from there,
but I ended up with an implementation that is quite different, since at the time I had not
discovered it yet. Toy's REPL also uses antirez's
[linenoise](https://github.com/antirez/linenoise) library.

## A Taste

```toy
\ Words can receive other programs as values. This one keeps the even
\ numbers, squares them, and folds the result into a single value.
'evens-squared-sum [
    [ 2 % 0 == ] filter
    [ square ] map
    0 swap [ + ] fold
] def

[ 1 2 3 4 5 6 ] evens-squared-sum print   \ 56
```

## Getting Started

### Release

Download and extract a release, then add its `bin` directory to `PATH` or use
the platform installer described in the [installation guide](./docs/installation.md).
Start the REPL, run a package, or evaluate a standalone file with the CLI. The
[REPL](./docs/repl.md) is a good way to start learning Toy since the language
is self-documenting. There are also [examples](./examples/) that showcase some
of the coolest features.

```console
toy
toy folder
toy --file script.toy
toy --eval "1 2 + print"
toy --tdb
```

The SDK includes a formatter, language server, debugger adapter, and
Tree-sitter grammar; the [editor guide](./docs/editor.md) shows how to use them.
See the [installation guide](./docs/installation.md) for more details
about the content of the SDK.

### Build Manually

Nob is the repository's build system. Bootstrap it once with a C compiler,
then use it to build Toy, run regressions, or stage a locally installable SDK.
The commands below use `nob` as the executable name; use its actual path or
platform suffix when needed.

```console
git clone https://github.com/lorenzotumini/toy.git
cd toy

cc nob.c -o nob
nob build
nob test
nob dist
```

The staged SDK is under `dist/toy`; install it as described in the
[installation guide](./docs/installation.md).

## Postfix Notation

An expression can be viewed as a
[tree](https://en.wikipedia.org/wiki/Abstract_syntax_tree). Infix, prefix, and
postfix notations mainly differ in when they visit the operator relative to its
children. For `1 + (2 * 3)`, the three common orders are:

| Style               | Traversal                         | Example         |
| ------------------- | --------------------------------- | --------------- |
| Infix               | left child, operator, right child | `1 + (2 * 3)`   |
| Prefix (Lisp)       | operator before children          | `(+ 1 (* 2 3))` |
| Postfix (Forth/Toy) | children before operator          | `1 2 3 * +`     |

In Toy, values are pushed as they are read. When `*` appears, it consumes
`2 3` and leaves `6`; when `+` appears, it consumes `1 6` and leaves `7`.
That is the whole syntax: data first, then the word that transforms it.

## Language Tour

### Words Consume and Produce Values

```toy
2 3 + print   \ 5
```

Words consume their inputs unless their documentation says otherwise. Use
ordinary stack words such as `dup`, or combinators such as `keep`, when an input
must remain available:

```toy
"hello" len              \ leaves 5
"hello" dup len          \ leaves "hello" 5
[ 1 2 3 ] [ len ] keep   \ leaves [1 2 3] 3
```

### Vectors Are Programs

A vector is also a quotation: a program that can be stored, passed, and run
later. `dup` runs immediately, `[ dup ]` delays that call, and `'dup` pushes the
name `dup` as data. Words that expect code can use either a quotation such as
`[ square ]` or the name of an existing word such as `'square`.

```toy
[ 1 2 + ] exec       \ leaves 3
[ 1 2 + ] i          \ alias

\ Give a quotation a name to define a new word.
'square [ dup * ] def
'cube [ dup square * ] def

5 square print       \ 25
3 cube print         \ 27

\ A named word can be passed without wrapping it in a one-item quotation.
5 'succ 'square bi   \ leaves 6 25
1 2 4 [ + ] dip      \ leaves 3 4
5 [ 1 + ] keep       \ leaves 5 6
```

Names remain data unless a word explicitly treats them as code.
Consequently, `[ dup ]` is a quotation that calls `dup`, while `[ 'dup ]` is
a quotation that contains its name. Toy preserves that difference when programs
inspect other programs:

```toy
[ dup ] first type-of print    \ call
[ 'dup ] first type-of print   \ symbol
```

The first value is a call instruction; the second is a symbol. Most programs do
not need to construct calls directly, but when they do `>call` makes runtime program
generation possible (see [homoiconicity](https://en.wikipedia.org/wiki/Homoiconicity)).
The [data-model reference](./docs/data-model.md) has a section describing the exact
representations and conversions.

### Captures Improve Usability

Captures offer names when a sequence of stack operations would be harder to
read:

```toy
'hypot2 [ | x y | $x $x * $y $y * + ] def
3 4 hypot2 print     \ 25

'my-swap [ | a b | $b $a ] def
```

They are also very useful when working with functions to express clearly
all the arguments and rearrange them.

```toy
'foo [ | string num bool |
    [ $bool ] [ $num $string "<{} {}>\n" printf ] if
] def

"hello" 10 true foo   \ <hello 10>
```

A capture lives only while its word or quotation is running, although code
called from inside may read it with `$name`.
An inner capture may hide an outer one, but it cannot update it, and a returned
quotation does not keep captures alive like a closure would.
Captures clarify the stack; they are not imperative variables.

### Control Is Built from Words

```toy
5 [ 0 > ] [ "positive" print ] if

10 [ 0 > ] [ dup print 1 - ] while drop

\ Recursion schemes can express algorithms without naming recursive helpers.
'qsort [
    [ len 2 < ]
    []
    [ uncons [ > ] split ]
    [ swapd cons concat ]
    binrec
] def

'merge-sort [
    [ len 2 < ]
    []
    [ split-mid ]
    [ [ < ] merge ]
    binrec
] def
```

Predicates used by words such as `if`, `while`, `filter`, `split`, and `merge`
observe the surrounding stack. For example, `[ 0 > ]` can test the top value
without consuming it: Toy reads the boolean result and then restores the stack
that existed before the predicate ran. Output, file writes, and other side
effects performed by a predicate are not undone.

The [combinator reference](./docs/combinators.md) covers the less obvious
control, recursion, and collection combinators in more depth.

### Versatile Collections

```toy
\ Vectors are ordered arrays and also the quotation representation.
[ 1 2 3 ] 4 push-back       \ leaves [1 2 3 4]
[ 1 2 3 ] pop-back          \ leaves [1 2] 3

\ Lists are persistent, front-oriented sequences.
( 1 2 3 ) uncons            \ leaves 1 (2 3)
0 ( 1 2 3 ) cons            \ leaves (0 1 2 3)

\ Strings participate in the same sequence vocabulary.
"abc" first                 \ leaves "a"
"ab" "c" push-back          \ leaves "abc"

\ Enjoy some concatenative power
"  alpha,beta,gamma  " trim "," split [ upper ] map "-" join print

\ Toy also supports maps, sets
{ 'name "Ada" 'age 36 } 'name get print
#{ "red" "green" } #{ "green" "blue" } union items sort print

\ Deques and pqueues have constructors instead of a dedicated syntax
[ 1 2 3 ] >deque 0 push-front print
[ [ 10 "low" ] [ 1 "urgent" ] ] >pqueue pairs print
```

Vectors provide indexing and efficient operations at the back; lists provide
cheap operations at the front and can share their tails. Strings are byte
sequences rather than Unicode strings, so one string item is a one-byte string.
Maps and sets preserve insertion order, deques support both endpoints, and
priority queues return the lowest priority first.

Integers are signed 64-bit values and floats use double precision. Mixed
comparisons preserve exact integer ordering where possible, even when a large
integer cannot be represented exactly as a double. The
[data-model reference](./docs/data-model.md) gives the full collection
contracts and complexity guarantees.

### A Practical Runtime

Toy includes file and process operations, clocks, environment access, and
introspection. These words still follow the language's value-oriented style:
`see` and `doc` return strings instead of deciding how to display them.

```toy
"todo.txt" "ship README\nrun tests\n" write-file
"todo.txt" read-lines [ upper ] map ", " join print
"todo.txt" file-exists? [ "todo.txt is on disk" print ] if

'square [ dup * ] def
'square see print

[ 1 "two" 'three [ 4 ] ] [ type-of ] map print
[ 1 + ] callable? print
"abc" sequence? print

pwd print
"echo from Toy shell" shell [ | out status | $status 0 == [ $out trim print ] if ] exec
```

The display words `.`, `.s`, and `.S` are observers: they print without
changing the stack. `repr` returns an escaped, source-like string
(makes me think of [quines](examples/quines/quine.toy)).
`print` writes one value literally with a newline; `printf` interprets `{}`
placeholders and adds no newline, while `format` returns the same substitution
as a string. `>int` and `>float` convert strict numeric text from input, files,
or process arguments. Comments use `\` to the end of a line
or `/* ... */` for a block.

## Packages

Toy packages are directories. Every `.toy` file directly inside a package
begins with the same declaration, definitions are public by default, and an
imported package is accessed through its declared name:

```toy
\ math/arithmetic.toy
'math package
'double [ 2 * ] def
'helper [ 1 + ] def
'helper private
```

```toy
\ app/main.toy
'main package
"../math" import

'main [ 21 math.double print ] def
```

Run it with `toy app`. Use `"../math" 'm import-as` when a local
alias is useful. Relative and absolute imports name exact directories;
Toy's fixed core-package directory is accessible with the core directive,
for example `"core:json" import` or `"core:ffi" import`.

`core:json` is a strict Toy-written JSON codec:

```toy
"core:json" import
"{\"name\":\"Ada\",\"active\":true}" json.decode
dup "name" get print
json.encode print
```

See the [JSON reference](./docs/json.md) for its value mapping, explicit null
value, UTF-8 rules, and error behavior. The runnable
[`examples/json.toy`](./examples/json.toy) demonstrates decoding, collection
processing, encoding, and recovery from invalid input.

Package top level is declaration-only, so files can be split without creating
an execution order. The CLI invokes the public `main` word of a package named
`main`; initialization elsewhere is explicit. See the
[package reference](./docs/packages.md) for the full model, standalone
evaluation, and native-library workflows.

## C Interop

A C program can embed the runtime, evaluate source, call Toy words,
exchange values, and add its own words. The smallest complete host is
[`examples/embedding/embed.c`](./examples/embedding/embed.c); the
[embedding guide](./docs/embedding.md) explains values, ownership, callbacks,
and errors.

A Toy package can contain Toy source, a C extension, or both. An extension uses the
same single `toy.h` header as an embedding host and compiles to a shared library
beside the package's `toy.package` manifest. The SDK's `toy-c-package` command
performs that build, while `toy-bindgen` generates extension code from an
explicit description of a C API. The [basic package](./examples/packages/basic/)
is minimal example; the [SQLite](./examples/packages/sqlite/) and
[Raylib](./examples/packages/raylib/) examples show how resources give foreign
handles Toy lifetimes.

For direct calls, `core:ffi` can open a shared library and bind fixed C
signatures at run time:

```toy
"core:ffi" 'c import-as

"hello"
"msvcrt.dll" c.open
"strlen" "cstr -> usize" c.bind
c.call print
```

The library name is platform-specific. [Using C libraries](./docs/c-libraries.md)
compares dynamic FFI, generated bindings, and handwritten extensions. The
[binding manifest reference](./docs/bindgen.md) documents advanced generated
wrappers.

## Built-in Words

<!-- BEGIN GENERATED BUILTIN TABLE -->
| Category                    | Words |
| --------------------------- | ----- |
| Stack                       | `dup`, `drop`, `swap`, `over`, `rot`, `swapd`, `nip`, `tuck`, `pick`, `roll`, `empty` |
| Math                        | `+`, `-`, `*`, `/`, `%`, `mod`, `neg`, `abs`, `max`, `min`, `sqrt`, `pow`, `exp`, `log`, `log10`, `sin`, `cos`, `tan`, `floor`, `ceil`, `round`, `pred`, `succ`, `square`, `cube`, `pi`, `e`, `tau`, `inf`, `nan`, `inf?`, `nan?`, `rand` |
| Logic / Bitwise             | `and`, `or`, `xor`, `not`, `shl`, `shr` |
| Comparison                  | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| Control                     | `exec`, `i`, `if`, `ifelse`, `while`, `cond`, `try`, `error` |
| Combinators                 | `app2`, `infra`, `cleave`, `construct`, `replicate`, `times`, `dip`, `keep`, `bi`, `linrec`, `binrec`, `genrec`, `treerec` |
| Sequence Combinators        | `each`, `map`, `fold`, `filter`, `some`, `all`, `split`, `merge` |
| Ordered Collections         | `>vector`, `>list`, `>string`, `>deque`, `at`, `set-at`, `slice`, `take`, `skip`, `len`, `concat`, `reverse`, `split-mid`, `range`, `empty?` |
| Collection Endpoints        | `first`, `last`, `rest`, `uncons`, `cons`, `push-front`, `push-back`, `pop-front`, `pop-back` |
| Sequence Search / Ordering  | `contains?`, `index-of`, `unique`, `sort` |
| Strings / Characters        | `join`, `trim`, `starts-with?`, `ends-with?`, `replace`, `format`, `upper`, `lower`, `char?`, `>char`, `char-code`, `letter?`, `digit?`, `alnum?`, `space?`, `upper?`, `lower?`, `punct?` |
| Maps / Sets                 | `>map`, `>set`, `has?`, `get`, `get-or`, `assoc`, `dissoc`, `keys`, `values`, `pairs`, `items`, `insert`, `remove` |
| Set Algebra                 | `union`, `intersection`, `difference`, `symmetric-difference`, `subset?`, `proper-subset?`, `superset?`, `proper-superset?`, `disjoint?` |
| Priority Queues             | `>pqueue`, `pq-push`, `pq-peek`, `pq-pop` |
| Types                       | `type-of`, `bool?`, `int?`, `float?`, `>int`, `>float`, `string?`, `symbol?`, `call?`, `vector?`, `list?`, `map?`, `set?`, `deque?`, `pqueue?`, `resource?`, `number?`, `sequence?`, `callable?` |
| Definitions / Introspection | `def`, `private`, `word?`, `var?`, `body`, `>symbol`, `>call`, `name`, `words`, `see`, `doc`, `search-words`, `repr` |
| Console                     | `printf`, `print`, `.`, `.s`, `.S`, `read-key`, `read-line`, `clear` |
| Packages                    | `package`, `import`, `import-as` |
| Files                       | `read-file`, `write-file`, `delete-file`, `read-lines`, `file-exists?` |
| Environment / Processes     | `argc`, `argv`, `env?`, `get-env`, `set-env`, `pwd`, `shell`, `exit` |
| Time                        | `sleep`, `unix-time`, `local-time`, `utc-time`, `cpu-time`, `monotonic-ns` |
<!-- END GENERATED BUILTIN TABLE -->

Words appear under their main idea even when they work on more than one type.
For example, `push-back` works on sequences and deques, while `pairs` works on
maps and priority queues.

## License

MIT

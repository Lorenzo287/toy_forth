# Toy Data Model

This document explains what Toy's values mean, which words work across several
collection types, and what performance to expect from each representation.

At the language level, updating a collection produces a value; a program cannot
observe whether the runtime reused storage or made a copy. The implementation
details that make this efficient are collected at the end of the guide.

## Representations and Syntax

| Representation | Syntax / constructor | Main role |
| -------------- | -------------------- | --------- |
| bool, int, float | literals and numeric words | scalar values |
| string | `"text"` | byte sequence; one character is a one-byte string |
| symbol | `'name` | word names and symbolic data; can name code to run |
| call | bare `name` inside code, `>call` | instruction to run a named word |
| vector | `[ 1 2 3 ]` | indexed sequence; can also be run as a program |
| list | `( 1 2 3 )` | persistent front-oriented sequence |
| map | `{ 'name "Ada" 'age 36 }`, `>map` | insertion-ordered key/value lookup |
| set | `#{ "red" "green" }`, `>set` | insertion-ordered membership |
| deque | `>deque` | efficient front and back endpoints |
| priority queue | `>pqueue` | minimum-priority access |
| resource | C extension or embedding API | opaque, typed foreign handle |

Strings are byte sequences, not Unicode strings. String literals accept `\n`,
`\r`, `\t`, `\"`, `\\`, and `\xHH`; unknown escapes are errors.

Vectors are also quotations. Lists are sequence data only: `( 1 2 + )` is a
list containing data, not executable code.

### Names and Code

Toy keeps a name separate from an instruction that calls the named word:

| Form | What Toy does |
| ---- | ------------- |
| `dup` | calls `dup` immediately |
| `'dup` | pushes the name `dup` |
| `[ dup ]` | pushes a quotation containing an instruction to call `dup` |
| `[ 'dup ]` | pushes a quotation containing the name `dup` as data |

This distinction is usually invisible because a quotation simply runs its call
instructions. It becomes visible when code is inspected or assembled at
runtime: `[ dup ] first` returns a value whose type is `call`, whereas
`[ 'dup ] first` returns a `symbol`.

`>symbol` converts text or a call into a symbol. `>call` converts a symbol into
an instruction that can be inserted into a quotation. The conversion is
explicit so that moving ordinary names through a program never makes them run
by accident.

Secondary structures have no literal syntax because their constructors validate
runtime data and establish invariants:

```toy
\ No literal syntax
[ 1 2 3 ] >deque
[ [ 10 "low" ] [ 1 "urgent" ] ] >pqueue

\ Can also express map and set with constructors instead of literal syntax
[ [ 'name "Ada" ] [ 'age 36 ] ] >map
[ 1 2 2 3 ] >set
```

## Shared Operations

Toy uses the same word when several types support the same idea. For example,
`len` works on vectors, lists, and strings, while `exec` can run a quotation or
the name of a defined word. This guide calls each shared kind of operation a
*capability*; predicates such as `sequence?` and `callable?` let a program ask
whether a value supports one.

| Capability | Accepted representations | Main words |
| ---------- | ------------------------ | ---------- |
| callable | vector quotation, or a symbol/call naming a word | `exec`, `i`, `dip`, `keep`, `bi`, `map`, `filter`, `if`, `while`, recursion combinators |
| sequence | vector, list, string | `len`, `first`, `last`, `rest`, `uncons`, `concat`, `reverse`, `map`, `fold`, `filter`, `split`, `merge`, `sort`, `unique` |
| indexed | vector, string | `at`, `set-at`, `slice` |
| persistent front | list | `cons`, `rest`, `uncons` |
| back stack | vector, deque | `push-back`, `pop-back`, `last` |
| associative | map | `has?`, `get`, `get-or`, `assoc`, `dissoc`, `keys`, `values`, `pairs` |
| membership | set, map keys | `has?`, `insert`, `remove`, `items`, set algebra |
| priority | priority queue | `pq-push`, `pq-peek`, `pq-pop`, `pairs` |

Shared vocabulary means shared semantics, not identical complexity. For
example, list `uncons` is O(1) because it can share the tail, while vector
`uncons` is O(n) because it returns an independent vector.

## Sequence Conventions

Sequence-transforming words preserve the input family when the family is part
of the input contract:

| Word | Result family |
| ---- | ------------- |
| `map`, `filter`, predicate `split`, `merge` | same as input sequence |
| `take`, `skip`, `concat`, `reverse`, `unique`, `sort` | same as input sequence |
| `set-at` | vector or string, matching the input |

Projection words return vectors as the default interchange sequence:

- `range`
- string separator `split`
- `keys`, `values`, `pairs`
- `items`
- `read-lines`
- `argv`
- `words`, `search-words`

Endpoint destructors return values in constructor order so the matching
constructor can rebuild the original value:

```toy
[ 1 2 3 ] pop-back push-back       \ leaves [1 2 3]
( 1 2 3 ) uncons cons              \ leaves (1 2 3)
[ 1 2 3 ] >deque pop-front push-front
```

Selectors such as `first`, `last`, and endpoint operations consume their
inputs unless the word explicitly says otherwise. Use `dup`, `keep`, or `bi`
when the original collection should remain available. `pq-peek` is the named
observer exception: it preserves the priority queue and returns the next
priority/value pair.

## Conversion and Interop

Conversions that change observable behavior are explicit:

```toy
[ 1 2 3 ] >list          \ choose linked-list behavior
( 1 2 3 ) >vector        \ choose indexed vector behavior
[ "a" "b" "c" ] >string  \ validate one-byte strings and join
[ 1 2 2 3 ] >set         \ intentionally collapse duplicates
( 1 2 3 ) >deque         \ choose front/back endpoint behavior
[ [ 10 "low" ] [ 1 "urgent" ] ] >pqueue pairs >pqueue
"42" >int                \ parse strict decimal integer text
"3.5" >float             \ parse strict finite floating-point text
42.0 >int                 \ exact because the float is integral
```

`>int` also accepts integers unchanged and finite floats that are exactly
integral and within the signed 64-bit range. Choose `floor`, `ceil`, or `round`
before `>int` when discarding a fractional part is intentional. `>float`
accepts floats unchanged and converts integers explicitly, with the normal
precision limits of double-precision representation. Numeric strings consume
their entire input and do not accept surrounding whitespace; use `trim` when
that policy is desired.

`contains?` and `index-of` use item equality for vectors and lists. For strings,
the second argument is a substring; the empty substring is found at byte index
zero, and `index-of` returns `-1` when absent.

`starts-with?` and `ends-with?` accept empty prefixes and suffixes. `replace`
substitutes every non-overlapping byte substring from left to right and rejects
an empty search string. These operations follow Toy's byte-string model; they
do not apply Unicode case or grapheme rules.

`>char` accepts integer codes from 0 through 255. `char-code` is its inverse,
and `read-key` returns the same one-byte string representation.
Numeric parsing is separate: `"7" >int` returns `7`, while `"7" char-code`
returns the byte code `55`.

## Equality, Hashing, Sorting

Structural equality exists for vectors, lists, maps, sets, deques, and priority
queues. Representation matters: `[ 1 2 ]` and `( 1 2 )` are not equal.

Hashable values, and therefore valid map keys and set items, are:

- bool
- int
- string
- symbol

Call instructions are deliberately not hashable; convert one with `>symbol`
when its name is intended as a key. Floats and structural values compare for
equality but are not hash keys yet.
Resources compare by wrapper identity and are not hashable. Their copied type
tag is visible in the display form `<resource:type.name>`, but the foreign
pointer is never exposed to Toy code.
This avoids unresolved policy questions around NaN, signed zero, and cached
structural hashes.

Natural `sort` is stable. Numeric sequences may mix integers and floats, but
NaN is rejected because it cannot participate in a total order. Strings sort by
byte value. `unique` preserves first occurrence; it is expected O(n) for
hashable scalar values and falls back to equality scanning for unhashable
values.

## Complexity Summary

### Vectors and Strings

Vectors are array-backed and optimized for indexing and back-stack behavior.
Strings are flat byte arrays with the same indexed/read conventions.

| Operation | Typical cost |
| --------- | ------------ |
| `len`, `empty?`, `at` | O(1) |
| `first`, `last` | O(1) |
| `push-back`, `pop-back` on a unique chain | O(1) / amortized O(1) |
| `push-back`, `pop-back`, `set-at` on a shared value | O(n) copy |
| `concat` | O(length(right)) when reusing a unique left value, otherwise O(n+m) |
| `slice`, `take`, `skip`, `reverse` | O(n) over the produced range |
| vector `uncons`, string front `cons` | O(n) |

Removing from the back does not shrink vector/string capacity; this keeps
repeated stack-style use amortized efficient.

### Lists

Lists are immutable singly linked values. Wrappers cache length, and nodes are
independently refcounted so tails can be shared.

| Operation | Typical cost |
| --------- | ------------ |
| `len`, `empty?`, `first` | O(1) |
| `cons` | O(1), shares the old list |
| `rest`, `uncons` | O(1), shares the suffix |
| `last`, `push-back`, `take`, `reverse` | O(n) |
| `skip` | O(n) traversal, then shares the suffix |
| `concat` | O(length(left)), shares the right |
| `sort` | O(n log n) |

Build lists in forward order by prepending and reversing once,
this keeps the complexity linear, while appending repeatedly at
the end has quadratic complexity.

```toy
( ) [ 1 2 3 4 ] [ swap cons ] fold reverse
```

### Maps and Sets

Maps and sets are insertion-ordered hash tables. The deterministic order is
intentional: it makes examples, tests, display, and interop stable.

| Operation | Typical cost |
| --------- | ------------ |
| `has?`, `get`, absent `dissoc`/`remove` | expected O(1) |
| `assoc`, `insert` on a unique value | expected amortized O(1) |
| update on a shared value | O(n) copy before update |
| present `dissoc`, present `remove` | O(n), because order is compacted |
| `keys`, `values`, `pairs`, `items` | O(n) |
| set algebra | expected O(n+m) |

`insert` and `remove` are set-specific membership updates, not positional
sequence operations. Mapping a set is explicit:

```toy
#{ 1 2 3 } items [ square ] map >set
```

### Deques

Deques use a circular buffer.

| Operation | Typical cost |
| --------- | ------------ |
| `push-front`, `push-back`, `pop-front`, `pop-back` on a unique chain | amortized O(1) |
| `first`, `last`, `len`, `empty?` | O(1) |
| endpoint update on a shared deque | O(n) copy before update |
| `items` | O(n) |

Ordinary output shows deques as display-only `deque[...]`; `repr` and `.S` use
the reconstructable `[...] >deque` form.

### Priority Queues

Priority queues are stable min-heaps: lower priorities are returned first, and
equal priorities keep insertion order.

| Operation | Typical cost |
| --------- | ------------ |
| `pq-peek` | O(1) |
| `pq-push`, `pq-pop` on a unique queue | O(log n) |
| update on a shared queue | O(n) copy before heap update |
| `>pqueue` | O(n) bottom-up heap construction |
| `pairs` | O(n log n), because it emits full priority order |

`pq-pop` returns `pqueue priority value`, matching `pq-push` input order. A
single `pq-pop pq-push` may move an equal-priority item behind its peers because
reinsertion is intentionally stable.

Ordinary output shows priority queues as display-only
`pqueue[[priority value] ...]`; `repr` and `.S` use the reconstructable
`[[priority value] ...] >pqueue` form.

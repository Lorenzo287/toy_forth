# Combinator Reference

Many Toy words take code as an input. Write `[ upper ]` when the code is a
quotation, or `'upper` when an existing word already does what you need. Code
that inspects a quotation may also extract one of its call instructions and
pass that directly, though most programs never need to do this. This guide uses
*callable* as a short name for any of these values because each one can be run.

For guidance on choosing among these words, captures, and collection-building
patterns, see [Writing Idiomatic Toy](./idioms.md).

Combinators that run a predicate follow Toy's observer rule: after reading its
boolean answer, Toy restores the surrounding stack. Side effects still happen.

## Direct Execution

`exec` executes a callable. `i` is its Joy-style alias.

```toy
[ 1 2 + ] exec       \ leaves 3
[ 1 2 + ] i          \ same
'upper callable?     \ true when upper is defined
```

## Stack Context

Use these when a callable should run with a specific view of the stack.

| Word | Use |
| ---- | --- |
| `dip` | hide the top value while a callable works below it |
| `keep` | run a callable on a value while keeping the original |
| `bi` | run two callables independently on one value |
| `app2` | run one callable independently on two values |
| `cleave` | run several projections and leave all outputs |
| `construct` | run several projections and collect outputs in a vector |
| `infra` | run a quotation using a vector as a temporary stack |

```toy
1 2 10 [ + ] dip                     \ leaves 3 10
10 [ 1 + ] keep                      \ leaves 10 11
10 [ 1 + ] [ 2 * ] bi                \ leaves 11 20
3 4 [ square ] app2                  \ leaves 9 16

5 [ [ 1 + ] [ square ] [ 2 * ] ] cleave
\ leaves 6 25 10

5 [ [ 1 + ] [ square ] [ 2 * ] ] construct
\ leaves [6 25 10]

[ 2 3 4 ] [ + * ] infra              \ leaves [14]
```

`cleave`, `construct`, and `cond` use a vector to hold several pieces of code.
The outer vector organizes them; the inner items are what the word runs.

## Sequence Combinators

Vectors, lists, and strings share the sequence combinators. Strings are byte
sequences, so each string item is a one-byte string.

| Word | Use |
| ---- | --- |
| `each` | run a body for each item while threading the current stack |
| `map` | transform each item and preserve the sequence family |
| `fold` | reduce from left to right with an accumulator |
| `filter` | keep items whose predicate is true |
| `some` / `all` | test whether any/all items satisfy a predicate |
| `split` | partition by predicate, or split a string by separator |
| `merge` | merge two sorted sequences using an ordering predicate |

```toy
[ 1 2 3 4 ] 'square map              \ leaves [1 4 9 16]
( 1 2 3 4 ) 'square map              \ leaves (1 4 9 16)
"toy" 'upper map                     \ leaves "TOY"

[ 1 2 3 4 5 ] [ 2 % 0 == ] filter    \ leaves [2 4]
[ 1 2 3 ] 'number? all               \ leaves true
[ 1 "x" 3 ] 'string? some            \ leaves true

0 [ 1 2 3 4 ] [ + ] fold             \ leaves 10
"" "abc" [ concat ] each             \ leaves "abc"

3 [ 1 2 3 4 ] [ > ] split            \ leaves 3 [1 2] [3 4]
"a,b,c" "," split                    \ leaves ["a" "b" "c"]
[ 1 3 5 ] [ 2 4 6 ] [ < ] merge      \ leaves [1 2 3 4 5 6]
```

Use `each` for effects or stack-threading. Use `map` when each item should
produce exactly one collected result.

## Repetition and Branching

| Word | Use |
| ---- | --- |
| `times` | repeat a body while threading the current stack |
| `replicate` | run a body repeatedly from the same surrounding stack and collect one result each time |
| `if` / `ifelse` | execute branches from a boolean or predicate callable |
| `while` | repeat a body while a predicate callable is true |
| `cond` | choose the body from the first true predicate/body clause |
| `try` | restore the stack and pass an error message to a handler |

```toy
0 3 [ 1 + ] times                    \ leaves 3
3 [ "x" ] replicate                  \ leaves ["x" "x" "x"]

5 [ 0 > ] [ "positive" ] if          \ leaves 5 "positive"
5 [ 0 > ] [ "positive" ] [ "no" ] ifelse

3 [ 0 > ] [ 1 - ] while              \ leaves 0

5 [
    [ [ 0 < ] [ "negative" ] ]
    [ [ 0 == ] [ "zero" ] ]
    [ [ true ] [ "positive" ] ]
] cond                               \ leaves 5 "positive"

10 [ "bad input" error ] [ drop 0 ] try   \ leaves 10 0
```

`try` restores the stack that existed after the body and handler were consumed,
pushes the error message, then runs the handler. A handler that does not need
the message should begin with `drop`.

This restoration is explicit rather than a general property of errors.
Without an enclosing `try`, an error unwinds the current evaluation but does
not undo completed stack effects. Unfinished combinators release values hidden
in their continuation state, and the surviving stack is not guaranteed to
reconstruct their inputs. An error raised by the handler also propagates from
the handler's current stack; `try` does not restore its snapshot a second time.
Interruption and `exit` are not runtime errors, are not caught by `try`, and
also unwind without rollback.

## Recursion Schemes

The recursion schemes all consume callables directly.

| Word | Shape |
| ---- | ----- |
| `linrec` | test, base, before recursive call, after recursive call |
| `binrec` | test, base, split into two recursive problems, combine |
| `genrec` | fully general single-recursion shape |
| `treerec` | recursively transform a vector tree |

```toy
'fact [
    [ 1 <= ]
    [ ]
    [ dup pred ]
    [ * ]
    linrec
] def

5 fact                               \ leaves 120
```

```toy
'qsort [
    [ len 2 < ]
    [ ]
    [ uncons [ > ] split ]
    [ swapd cons concat ]
    binrec
] def

[ 3 1 4 1 5 2 ] qsort                \ leaves [1 1 2 3 4 5]
```

```toy
5 [ 0 == ] [ drop 1 ] [ dup pred ] [ * ] genrec
\ leaves 120

[ 1 [ 2 3 ] 4 ] [ ] [ 0 swap [ + ] fold ] treerec
\ leaves 10
```

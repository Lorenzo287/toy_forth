# Writing Idiomatic Toy

Idiomatic Toy makes the dataflow visible. Prefer direct composition and
combinators, use captures to name stable context, and choose a collection for
the operations performed while constructing it. These are defaults, not rules
to follow mechanically: clarity comes first, and performance claims should be
checked with a representative workload.

## Start with the Program Shape

Choose the combinator that already describes the result:

| Intent | First choice |
| --- | --- |
| Produce one result for every item | `map` |
| Select items without changing them | `filter` |
| Reduce a sequence to one evolving value | `fold` |
| Perform effects or thread a more general stack effect | `each` |
| Collect several values emitted onto a temporary stack | `infra` |
| Express regular recursion | `linrec`, `binrec`, `genrec`, or `treerec` |

For example, transforming a sequence does not need an explicit accumulator:

```toy
[ 1 2 3 4 ] 'square map
```

Use `fold` when the accumulator is part of the problem. Leave that accumulator
on the stack so `fold` can pass each new value directly to the next iteration:

```toy
[] [ 1 2 3 4 ] [ square push-back ] fold
{} [ { "id" 2 } { "id" 4 } ] [ dup "id" get swap assoc ] fold
```

The first body receives `vector item` and leaves the updated vector. The second
receives `map record`, derives the key, and leaves the updated map.

`infra` is useful when a quotation naturally emits several values rather than
returning one result per input item:

```toy
[] [ [ 1 2 3 4 ] 'square each ] infra
```

It runs the quotation with the vector as a temporary stack and returns the
resulting stack as a vector.

## Captures Name Context, Not Mutable Variables

Captures are most useful at a word boundary, especially when several inputs
would otherwise be hard to recognize:

```toy
'scale-about [ | values origin factor |
    $values [ $origin - $factor * $origin + ] map
] def
```

Here `origin` and `factor` are stable context for every mapped item. Their names
make the calculation easier to read without replacing the combinator.

Be careful when a capture is used like a mutable accumulator. Captures retain
their values; fetching a captured collection gives the stack another reference
to it. An update of that fetched vector, string, map, set, deque, or priority
queue must therefore preserve the captured value and copy before changing it.

```toy
\ Avoid: every fetch makes output shared before push-back.
'captured-build [ | values |
    [] $values [ | output item |
        $output $item push-back
    ] fold
] def

\ Prefer: fold threads the only reference to the evolving vector.
'threaded-build [ | values |
    [] $values [ push-back ] fold
] def
```

This distinction is irrelevant for scalars and often harmless for naturally
persistent lists. It matters in loops that repeatedly update copy-on-write
collections. Capturing the word's inputs is also fine: within each invocation,
the capture is stable even when a new value is passed to a recursive call.

Use a capture when it removes genuinely obscure shuffling. First check whether
`dip`, `keep`, `bi`, `app2`, `cleave`, `construct`, or a better factorization
expresses the same relationship directly.

Combinators differ in whether they thread or restore surrounding data. `dip`,
`keep`, `bi`, `fold`, `times`, and `each` pass the current stack forward through
their bodies. Their runtime continuations do not retain that stack merely to
undo an unhandled error; an enclosing `try` owns recoverable restoration.

Observer and projection combinators deliberately have a different contract.
On successful execution, predicates restore the stack after reading their
boolean, `map` and `replicate` isolate each produced result, and `app2`,
`cleave`, and `construct` apply branches independently. Their snapshots
implement that successful dataflow, but are released without restoration when
an error propagates. Only `try` establishes a recoverable stack transaction.
Side effects outside the data stack still happen.

When a loop repeatedly updates several collections, order the state so the
next collection to change is directly accessible. Put any necessary shuffling
in a small word with an explicit stack effect, and capture only the scalar that
needs a name:

```toy
\ order queue seen -- order queue seen node
'dequeue-visit [
    swap pop-front swapd | node |
    2 roll $node push-back rot rot
    $node
] def
```

This is usually clearer than either repeating the shuffle or capturing all
three collections. State order still matters for readability and for reaching
the next operation directly, but using `dip` for that reach no longer makes the
surrounding values shared by itself.

## Choose the Construction Representation

The final representation need not be the best construction representation.

| Need while building | Natural choice |
| --- | --- |
| Indexed access or back accumulation | vector |
| Cheap prepend, persistent history, or shared suffixes | list |
| Both front and back endpoints | deque |
| Lookup or duplicate-key detection | map |
| Membership and uniqueness | set |
| Repeated minimum-priority access | priority queue |
| Flat text from known fragments | sequence of strings followed by `join` |

Build a list in forward order by prepending and reversing once:

```toy
() [ 1 2 3 4 ] [ square swap cons ] fold reverse
```

Convert once at the boundary when another representation is required:

```toy
() [ 1 2 3 4 ] [ square swap cons ] fold reverse >vector
[ [ "a" 1 ] [ "b" 2 ] ] >map
[ 1 2 2 3 ] >set
```

For text, collect meaningful fragments and join once instead of repeatedly
appending to a captured string:

```toy
[ "to" "y" ] "" join
[ "red" "green" "blue" ] "," join
```

Direct stack-threaded `push-back` is also linear when the string or vector stays
unique. `join` is preferable when the program already thinks in fragments; a
vector is preferable when it needs indexed access while building.

Do not choose a specialized structure only because it exists. A conversion and
a more complex API are justified when the program actually uses the
structure's capability or complexity advantage.

## Keep Sharing Intentional

Toy updates are values: the runtime may reuse unique storage, but the program
cannot observe mutation. `dup`, a capture, or retaining an original collection
creates real sharing, so a later update may copy. That is exactly the right
behavior when both versions are needed:

```toy
[ 1 2 3 ] dup 4 push-back
\ leaves the original [1 2 3] and the new [1 2 3 4]
```

When only the updated version is needed, consume and replace the old value
directly. This keeps the source simple and lets the runtime reuse storage.

## Measure Patterns Before Changing the Language

[`benchmarks/construction-patterns.toy`](../benchmarks/construction-patterns.toy)
compares equivalent captured, stack-threaded, list, `infra`, and one-shot
conversion patterns. The recorded
[construction and JSON experiment](../benchmarks/results/2026-07-31-toy-construction-idioms.md)
shows why the distinction matters in real Toy code.

The standalone [graph-search examples](../examples/graphs/README.md) are one
larger application of these ideas to multiple evolving values. Their
[benchmark record](../benchmarks/results/2026-07-31-graph-state-threading.md)
first exposed accidental snapshot retention in `dip`, then confirmed that the
runtime fix made both BFS layouts linear. The separate
[control-ownership experiment](../benchmarks/results/2026-07-31-control-ownership.md)
audits the same distinction across the control vocabulary.

Use those results as guidance, not as a universal ranking. Small data, required
sharing, clearer domain structure, or a different mix of operations can change
the right choice. The [combinator reference](./combinators.md) describes the
available control shapes, while the [data model](./data-model.md) gives the
collection complexity contracts behind these idioms.

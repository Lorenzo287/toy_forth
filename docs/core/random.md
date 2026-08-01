# Random

The official `core:random` package provides bounded integers, unit floats,
sequence selection, deterministic seeding, and shuffling over the
interpreter's state-owned PCG32 stream.

The runnable [`examples/random.toy`](../../examples/random.toy) demonstrates
reproducible dice, choice, shuffling, and unit floats.

```toy
"core:random" import

1 7 random.int
[ "red" "green" "blue" ] random.choice
[ 1 2 3 4 5 ] random.shuffle
random.float
```

## API

| Word | Stack effect | Meaning |
| ---- | ------------ | ------- |
| `random.seed` | `seed --` | Deterministically reset the current state's stream from an integer. |
| `random.int` | `lower upper -- n` | Draw an unbiased integer from the half-open range `[lower, upper)`. |
| `random.float` | `-- f` | Draw a float from the half-open range `[0, 1)`. |
| `random.choice` | `sequence -- item` | Select one item uniformly from a non-empty vector, list, or string. |
| `random.shuffle` | `sequence -- sequence` | Apply a uniform Fisher-Yates shuffle and preserve the sequence type. |

Every new interpreter state is seeded from operating-system entropy. Streams
belong to their state, so creating or running another state does not perturb a
program's draws. `random.seed` replaces that automatic seed with a reproducible
one; the same integer then produces the same sequence on every supported
platform. It also resets the stream observed by the low-level `rand` and
`rand-int` builtins.

```toy
'rolls [ 6 [ 1 7 random.int ] replicate ] def

42 random.seed rolls
42 random.seed rolls
==  \ true
```

`random.int` accepts any signed 64-bit bounds for which `lower < upper`. Range
reduction uses rejection sampling rather than `%`, so every result has the
same probability even when the range width does not divide the generator's
domain. `random.float` uses 53 random bits, matching the precision available
in a Toy float.

`random.choice` rejects empty sequences. A string choice is a one-byte string,
consistent with Toy's sequence model. Choice is O(1) for vectors and strings;
choosing from a list is O(n) and uses an intermediate vector because lists do
not provide indexed access. `random.shuffle` returns a vector, list, or string
matching its input and does not observably mutate a retained original. It runs
in O(n) time. Vectors are shuffled through indexed value updates; when uniquely
owned, their storage can be reused with O(1) loop state. Lists and strings use
an intermediate vector and therefore require O(n) additional storage.

The package is intended for simulations, randomized algorithms, games, tests,
and sampling. PCG32 is not a cryptographic generator: do not use these words
for passwords, tokens, keys, nonces, or other security-sensitive material.

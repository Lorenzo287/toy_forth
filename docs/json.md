# JSON

The official `core:json` package encodes and decodes strict JSON without an
external dependency. It is implemented in Toy and operates on UTF-8 stored in
Toy's byte strings.

```toy
"core:json" import

"{\"name\":\"Ada\",\"scores\":[10,12]}" json.decode
dup "name" get print
json.encode print
```

## API

| Word | Stack effect | Meaning |
| ---- | ------------ | ------- |
| `json.decode` | `string -- value` | Decode exactly one JSON document. |
| `json.encode` | `value -- string` | Produce compact JSON. |
| `json.null` | `-- null` | Return the explicit JSON-null value. |
| `json.null?` | `value -- bool` | Test for that value. |

Parsing and encoding failures are ordinary Toy errors. A caller can attach
context or recover with `try`:

```toy
[ input json.decode ]
[ "invalid configuration: {}" format error ] try
```

The handler receives the original error message. In this example `input`
remains on the restored stack beneath it.

## Examples

Decode a document and use ordinary map and sequence words to inspect it:

```toy
"core:json" import

"{\"users\":[{\"name\":\"Ada\",\"active\":true}]}" json.decode
"users" get
[ "active" get ] filter
[ "name" get ] map
", " join print
```

Build a Toy value and encode it without constructing JSON text manually:

```toy
{}
"name" "Toy" assoc
"stable" false assoc
"next-release" json.null assoc
json.encode print
```

Use the error message supplied by `try` while returning an explicit fallback:

```toy
'decode-or-null [
    [ json.decode ]
    [ | source message |
        $source len $message "Invalid {}-byte JSON: {}" format print
        json.null
    ] try
] def

"{broken}" decode-or-null
```

The runnable [`examples/json.toy`](../examples/json.toy) combines nested data,
filtering and mapping, encoding, explicit null, and error recovery.

## Value Mapping

| JSON | Toy |
| ---- | --- |
| `null` | the symbol returned by `json.null` |
| boolean | bool |
| integer token | signed 64-bit int |
| token with a fraction or exponent | finite float |
| string | UTF-8 byte string |
| array | vector |
| object | insertion-ordered map with string keys |

`json.null` currently returns the explicit symbol `'json.null`. This is a
package-specific serialization value, not a general representation of absence
in Toy.

Integer tokens outside Toy's signed 64-bit range are rejected rather than
silently rounded to floats. Floating-point overflow and underflow are also
errors. Encoding rejects NaN and infinities because JSON has no representation
for them.

The decoder validates raw UTF-8, decodes JSON escapes, combines valid UTF-16
surrogate pairs, and rejects unpaired surrogates. The encoder validates Toy
strings before emitting them, escapes control bytes, quotes, and backslashes,
and otherwise preserves valid UTF-8 bytes.

Objects reject duplicate keys so their meaning is deterministic. Encoding
accepts vectors and lists as arrays and requires every map key to be a string.
Other Toy values, including sets, deques, priority queues, resources, calls,
and symbols other than `json.null`, are rejected.

The package intentionally has no permissive mode, pretty printer, streaming
interface, JSON5 extensions, schema support, or custom value hooks.

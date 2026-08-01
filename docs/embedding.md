# Embedding Toy in C

Embedding is the C-owned direction: the application keeps its normal `main`,
state, and event loop, while Toy supplies scripts and callable words. The Toy
SDK provides `include/toy.h` and a matching static `toy_runtime` library.

If Toy should own the program and call an existing C library, see
[Using C Libraries](./c-libraries.md) instead.

## A Small Host

Create one state, evaluate source, exchange values through its data stack, and
destroy the state when the application is done:

```c
#include "toy.h"

#include <inttypes.h>
#include <stdio.h>

int main(void) {
    toy_state *state = toy_state_new(NULL);
    if (!state) return 1;

    if (toy_eval(state, "score.toy", "'score [ 2 * 10 + ] def") != TOY_OK) {
        fprintf(stderr, "%s\n", toy_get_error(state));
        toy_state_free(state);
        return 1;
    }

    toy_push_int(state, 16);
    if (toy_call(state, "score") != TOY_OK) {
        fprintf(stderr, "%s\n", toy_get_error(state));
        toy_state_free(state);
        return 1;
    }

    int64_t score = 0;
    if (toy_get_int(state, 0, &score)) {
        printf("score = %" PRId64 "\n", score);
    }

    toy_state_free(state);
    return 0;
}
```

The complete [`examples/embedding/embed.c`](../examples/embedding/embed.c)
also registers a C package that Toy calls. Its
[example guide](../examples/embedding/README.md) gives direct compiler commands
for the SDK on Windows, Linux, and macOS.

## Calling Toy

`toy_eval()` runs an in-memory source string. Its source name is used in error
locations; definitions and stack values remain in the state across later
calls. `toy_call()` invokes a named word using the current stack.

Primitive values use direct push and getter functions. Stack depth zero is the
top value:

```c
toy_push_string(state, "Ada", 3);
toy_call(state, "greet");

const char *message = NULL;
size_t length = 0;
if (toy_get_string(state, 0, &message, &length)) {
    fwrite(message, 1, length, stdout);
}
toy_pop(state, 1);
```

Getters borrow values without consuming them. `toy_pop()` consumes and releases
stack values. Strings are byte spans rather than NUL-terminated C strings, and
their borrowed pointer should not be kept across stack mutation or execution.

`toy_import_package()` loads a package into the host root. An optional alias
selects its qualifier. `toy_run_package()` loads an executable package and
calls its public `main` word. Set `toy_state_config.core_package_path` when
embedded code needs `core:` packages; an embedder has no implied SDK location.

## Calling C from Toy

A native word is a normal C function that reads its inputs from the Toy stack
and pushes its results:

```c
static toy_status host_log(toy_state *state) {
    const char *message = NULL;
    size_t length = 0;
    if (!toy_get_string(state, 0, &message, &length)) {
        return toy_fail(state, "host.log expected a string");
    }

    fwrite(message, 1, length, stdout);
    fputc('\n', stdout);
    return toy_pop(state, 1) ? TOY_OK
                             : toy_fail(state, "host.log could not pop input");
}
```

Register a standalone root word with `toy_register_word()`, or group related
words into a package:

```c
static const toy_native_word host_words[] = {
    {
        .name = "log",
        .callback = host_log,
        .stack_effect = "message --",
        .description = "Write one string through the host logger.",
    },
};

static const toy_native_package host_package = {
    "host",
    host_words,
    sizeof(host_words) / sizeof(host_words[0]),
};

toy_register_package(state, &host_package);
```

Toy can then call `host.log`. Registration copies names and optional
documentation and makes all words public. The callback functions themselves
must remain valid for the lifetime of the state.

## Persistent and Structured Values

`toy_value_retain()` keeps any stack value across later execution. A retained
value is bound to its original state and must be released before that state is
destroyed:

```c
toy_eval(state, "callback.toy", "[ 3 * ]");
toy_value *callback = toy_value_retain(state, 0);
toy_pop(state, 1);

toy_push_int(state, 14);
toy_call_value(state, callback);  /* leaves 42 */
toy_value_release(callback);
```

The value accessors inspect retained primitives and resources.
`toy_sequence_get()` traverses vectors, lists, and strings;
`toy_map_entry()` traverses maps in insertion order. Returned items are new
retained values and must also be released.

Construction remains stack-oriented:

```c
toy_push_int(state, 3);
toy_push_int(state, 4);
toy_push_int(state, 5);
toy_make_vector(state, 3);  /* leaves [3 4 5] */

toy_push_string(state, "name", 4);
toy_push_string(state, "Ada", 3);
toy_make_map(state, 1);     /* leaves {"name" "Ada"} */
```

[`examples/embedding/values.c`](../examples/embedding/values.c) builds a map,
traverses a returned value, and retains a Toy quotation as a callback.

## Foreign Handles

A host can wrap an external handle without exposing its pointer to Toy:

```c
toy_status status = toy_push_resource(state, "game.texture", texture,
                                      destroy_texture, NULL);
if (status != TOY_OK) destroy_texture(texture, NULL);
```

Ownership transfers only when the push succeeds. The destructor runs exactly
once after the last Toy reference disappears. `toy_get_resource()` checks the
exact type name and returns a borrowed pointer; finish using that pointer before
consuming its resource value. Destructors must not re-enter the Toy state.

## Output, Errors, and Lifetime

Passing a `toy_state_config` to `toy_state_new()` can redirect normal output and
diagnostics independently. Callbacks receive borrowed byte spans, may receive
NUL bytes, and run synchronously. The buildable
[`callbacks.c`](../examples/embedding/callbacks.c) demonstrates both streams.

The main statuses are:

- `TOY_OK`: execution completed;
- `TOY_ERROR`: parsing, execution, or native code failed;
- `TOY_INTERRUPTED`: `toy_interrupt()` requested an unwind;
- `TOY_EXIT_REQUESTED`: Toy executed `exit`, leaving termination to the host.

`toy_get_error()` returns the latest parser or runtime message. Errors,
interruptions, and exit requests unwind the current invocation but do not roll
back definitions, completed stack effects, output, or other side effects.
Unwound frames release values they own privately, so the residual stack is
inspectable but is not a reconstruction of the invocation's inputs. The host
may inspect or repair it and reuse the state. Toy code that needs transactional
recovery from a runtime error must establish it explicitly with `try`;
interruption and exit are not caught.

The boundary has a few durable rules:

- separate states may execute concurrently, but one state is not safe for
  concurrent access;
- `toy_eval()`, `toy_call()`, package loading, and retained calls require an
  idle state;
- a native word must not re-enter the state that is currently calling it;
- borrowed strings and pointers do not survive arbitrary mutation;
- every retained value must be released before its state;
- allocation failure terminates the process;
- filesystem, environment, process, and shell words remain enabled.

Each state owns its boxed-object and persistent-list pools, execution
continuations, scratch storage, dictionary caches, and pseudorandom stream.
Closing one state therefore does not drain, invalidate, or contend on storage
owned by another live state. Host callbacks and intentional process-wide
effects—including default console output, environment changes, and subprocess
I/O—remain shared and need normal host-side coordination when used concurrently.

The public declarations and concise ownership comments in
[`include/toy.h`](../include/toy.h) are the complete API reference.

#include "toy.h"

#include <stdio.h>
#include <string.h>

static int report_error(toy_state *state, const char *operation) {
    fprintf(stderr, "%s failed: %s\n", operation,
            toy_get_error(state) ? toy_get_error(state) : "unknown error");
    return 1;
}

static bool value_is_callable(const toy_value *value) {
    toy_type type = toy_value_type(value);
    return type == TOY_TYPE_VECTOR || type == TOY_TYPE_SYMBOL ||
           type == TOY_TYPE_CALL;
}

static toy_status defer_message(toy_state *state, const toy_value *handler,
                                const char *message) {
    toy_status status = toy_push_string(state, message, strlen(message));
    if (status == TOY_OK) status = toy_defer_call(state, handler, 1);
    return status;
}

/* This stands in for a same-thread C library that invokes two callbacks before
 * returning to its Toy native word. Neither callback re-enters the VM. */
static toy_status emit_twice(toy_state *state) {
    toy_value *handler = toy_value_retain(state, 0);
    if (!handler || !value_is_callable(handler)) {
        toy_value_release(handler);
        return toy_fail(state, "host.emit-twice expected a callable");
    }
    if (!toy_pop(state, 1)) {
        toy_value_release(handler);
        return toy_fail(state, "host.emit-twice could not pop its callable");
    }

    toy_status status = defer_message(state, handler, "first native event");
    if (status == TOY_OK) {
        status = defer_message(state, handler, "second native event");
    }
    toy_value_release(handler);
    return status;
}

static const toy_native_word host_words[] = {
    {
        .name = "emit-twice",
        .callback = emit_twice,
        .stack_effect = "handler --",
        .description = "Queue two messages for a Toy handler.",
    },
};

static const toy_native_package host_package = {
    "host",
    host_words,
    sizeof(host_words) / sizeof(host_words[0]),
};

int main(void) {
    toy_state *state = toy_state_new(NULL);
    if (!state) {
        fputs("failed to create Toy state\n", stderr);
        return 1;
    }
    if (toy_register_package(state, &host_package) != TOY_OK) {
        int result = report_error(state, "registering host package");
        toy_state_free(state);
        return result;
    }

    const char *handler =
        "[ | message | $message \"handled: {}\\n\" printf ]";
    char source[256];
    int length =
        snprintf(source, sizeof(source), "%s host.emit-twice", handler);
    if (length < 0 || (size_t)length >= sizeof(source) ||
        toy_eval(state, "native-events.toy", source) != TOY_OK) {
        int result = report_error(state, "handling native events");
        toy_state_free(state);
        return result;
    }

    if (toy_eval(state, "host-event.toy", handler) != TOY_OK) {
        int result = report_error(state, "creating host handler");
        toy_state_free(state);
        return result;
    }
    toy_value *host_handler = toy_value_retain(state, 0);
    if (!host_handler || !toy_pop(state, 1) ||
        defer_message(state, host_handler, "idle host event") != TOY_OK) {
        toy_value_release(host_handler);
        int result = report_error(state, "queueing host event");
        toy_state_free(state);
        return result;
    }
    toy_value_release(host_handler);

    printf("queued host events: %zu\n", toy_deferred_count(state));
    if (toy_run_deferred(state) != TOY_OK) {
        int result = report_error(state, "running host events");
        toy_state_free(state);
        return result;
    }

    toy_state_free(state);
    return 0;
}

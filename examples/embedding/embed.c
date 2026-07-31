#include "toy.h"

#include <inttypes.h>
#include <stdio.h>

static toy_status hostLog(toy_state *state) {
    const char *message = NULL;
    size_t length = 0;
    if (!toy_get_string(state, 0, &message, &length)) {
        return toy_fail(state, "host.log expected a string");
    }

    fputs("Toy says: ", stdout);
    fwrite(message, 1, length, stdout);
    fputc('\n', stdout);

    if (!toy_pop(state, 1)) {
        return toy_fail(state, "host.log failed to pop its input");
    }
    return TOY_OK;
}

static const toy_native_word host_words[] = {
    {.name = "log", .callback = hostLog},
};

static const toy_native_package host_package = {
    "host",
    host_words,
    sizeof(host_words) / sizeof(host_words[0]),
};

static int reportError(toy_state *state, const char *operation,
                       toy_status status) {
    const char *message = toy_get_error(state);
    fprintf(stderr, "%s failed (status %d): %s\n", operation, (int)status,
            message ? message : "no diagnostic available");
    return 1;
}

int main(void) {
    toy_state *state = toy_state_new(NULL);
    if (!state) {
        fputs("failed to create Toy state\n", stderr);
        return 1;
    }

    toy_status status = toy_register_package(state, &host_package);
    if (status != TOY_OK) {
        int result = reportError(state, "native registration", status);
        toy_state_free(state);
        return result;
    }

    const char *program =
        "'score [ 2 * 10 + ] def\n"
        "\"runtime initialized\" host.log";
    status = toy_eval(state, "embed.toy", program);
    if (status != TOY_OK) {
        int result = reportError(state, "Toy evaluation", status);
        toy_state_free(state);
        return result;
    }

    status = toy_push_int(state, 16);
    if (status == TOY_OK) status = toy_call(state, "score");

    int64_t score = 0;
    if (status != TOY_OK) {
        int result = reportError(state, "Toy word call", status);
        toy_state_free(state);
        return result;
    }
    if (!toy_get_int(state, 0, &score)) {
        fputs("Toy word did not return an integer\n", stderr);
        toy_state_free(state);
        return 1;
    }

    printf("C received score: %" PRId64 "\n", score);
    toy_state_free(state);
    return 0;
}

#define TOY_EXTENSION_IMPLEMENTATION
#include "toy.h"

#include <stdint.h>

static toy_status random_seed_word(toy_state *state) {
    int64_t seed = 0;
    if (!toy_get_int(state, 0, &seed)) {
        return toy_fail(state, "'random.seed' expected an integer seed");
    }
    if (!toy_pop(state, 1)) {
        return toy_fail(state, "random.seed failed to pop its seed");
    }
    toy_random_seed(state, seed);
    return TOY_OK;
}

static toy_status random_int_word(toy_state *state) {
    int64_t lower = 0;
    int64_t upper = 0;
    if (!toy_get_int(state, 1, &lower) ||
        !toy_get_int(state, 0, &upper)) {
        return toy_fail(state, "'random.int' expected two integer bounds");
    }
    if (lower >= upper) {
        return toy_fail(
            state,
            "'random.int' upper bound must be greater than lower bound");
    }

    int64_t result = 0;
    if (!toy_random_int(state, lower, upper, &result)) {
        return toy_fail(state, "random.int failed to generate a value");
    }
    if (!toy_pop(state, 2)) {
        return toy_fail(state, "random.int failed to pop its bounds");
    }
    return toy_push_int(state, result);
}

static toy_status random_float_word(toy_state *state) {
    const int64_t denominator = INT64_C(9007199254740992);
    int64_t numerator = 0;
    if (!toy_random_int(state, 0, denominator, &numerator)) {
        return toy_fail(state, "random.float failed to generate a value");
    }
    return toy_push_float(state, (double)numerator / (double)denominator);
}

#include "generated_words.inc"

static const toy_extension random_extension = {
    sizeof(toy_extension),
    "random",
    random_words,
    sizeof(random_words) / sizeof(random_words[0]),
};

TOY_EXTENSION_EXPORT const toy_extension *toy_extension_init(
    uint32_t abi_version, const toy_extension_api *api) {
    if (!toy_extension_bind(abi_version, api)) return NULL;
    return &random_extension;
}

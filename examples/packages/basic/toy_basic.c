#define TOY_EXTENSION_IMPLEMENTATION
#include "toy.h"

static toy_status twice(toy_state *state) {
    int64_t value = 0;
    if (!toy_get_int(state, 0, &value)) {
        return toy_fail(state, "basic.twice expected an integer");
    }
    if (!toy_pop(state, 1)) {
        return toy_fail(state, "basic.twice failed to pop its input");
    }
    return toy_push_int(state, value * 2);
}

static const toy_native_word words[] = {
    {.name = "twice", .callback = twice},
};

static const toy_extension extension = {
    sizeof(toy_extension),
    "basic",
    words,
    sizeof(words) / sizeof(words[0]),
};

TOY_EXTENSION_EXPORT const toy_extension *toy_extension_init(
    uint32_t abi_version, const toy_extension_api *api) {
    if (!toy_extension_bind(abi_version, api)) return NULL;
    return &extension;
}

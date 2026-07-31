#include "toy.h"

static toy_status unavailable(toy_state *state) {
    (void)state;
    return TOY_ERROR;
}

static const toy_native_word words[] = {
    {.name = "unavailable", .callback = unavailable},
};

static const toy_extension extension = {
    sizeof(toy_extension) + 1,
    "bad",
    words,
    sizeof(words) / sizeof(words[0]),
};

TOY_EXTENSION_EXPORT const toy_extension *toy_extension_init(
    uint32_t abi_version, const toy_extension_api *api) {
    (void)abi_version;
    (void)api;
    return &extension;
}

#ifdef _WIN32
#include <stdlib.h>
#else
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#endif

#include "toy.h"

#include <stdio.h>
#include <string.h>

#ifdef STB_LEAKCHECK
#include "tf_alloc.h"
#endif

#define CHECK(condition, message)                                      \
    do {                                                               \
        if (!(condition)) {                                            \
            fprintf(stderr, "ffi package check failed: %s\n", message); \
            return 1;                                                  \
        }                                                              \
    } while (0)

static void discard_diagnostic(void *userdata, const char *data,
                               size_t length) {
    (void)userdata;
    (void)data;
    (void)length;
}

static toy_status push_path(toy_state *state, const char *path) {
    return toy_push_string(state, path, strlen(path));
}

int main(int argc, char **argv) {
    CHECK(argc == 3, "core directory and fixture path arguments");

    toy_state_config config = {
        .diagnostic = discard_diagnostic,
        .core_package_path = argv[1],
    };
    toy_state *state = toy_state_new(&config);
    CHECK(state, "state creation");
    CHECK(toy_import_package(state, "core:ffi", NULL) == TOY_OK,
          "import core FFI package");

    CHECK(toy_push_int(state, 20) == TOY_OK, "push add left");
    CHECK(toy_push_int(state, 22) == TOY_OK, "push add right");
    CHECK(push_path(state, argv[2]) == TOY_OK, "push add library path");
    CHECK(toy_eval(state, "<ffi-add>",
                   "ffi.open \"toy_ffi_add_i32\" "
                   "\"i32 i32 -> i32\" ffi.bind ffi.call") == TOY_OK,
          "call integer function");
    int64_t integer = 0;
    CHECK(toy_get_int(state, 0, &integer) && integer == 42,
          "integer result");
    CHECK(toy_pop(state, 1), "pop integer result");

    CHECK(push_path(state, argv[2]) == TOY_OK, "push i8 library path");
    CHECK(toy_eval(state, "<ffi-i8>",
                   "ffi.open \"toy_ffi_negative_i8\" "
                   "\"-> i8\" ffi.bind ffi.call") == TOY_OK,
          "call narrow signed function");
    CHECK(toy_get_int(state, 0, &integer) && integer == -7,
          "narrow signed result");
    CHECK(toy_pop(state, 1), "pop narrow signed result");

    CHECK(push_path(state, argv[2]) == TOY_OK, "push u32 library path");
    CHECK(toy_eval(state, "<ffi-u32>",
                   "ffi.open \"toy_ffi_large_u32\" "
                   "\"-> u32\" ffi.bind ffi.call") == TOY_OK,
          "call narrow unsigned function");
    CHECK(toy_get_int(state, 0, &integer) && integer == UINT32_MAX,
          "narrow unsigned result");
    CHECK(toy_pop(state, 1), "pop narrow unsigned result");

    CHECK(toy_push_float(state, 2.5) == TOY_OK, "push float value");
    CHECK(toy_push_int(state, 4) == TOY_OK, "push float scale");
    CHECK(push_path(state, argv[2]) == TOY_OK, "push float library path");
    CHECK(toy_eval(state, "<ffi-float>",
                   "ffi.open \"toy_ffi_scale_f64\" "
                   "\"f64 i32 -> f64\" ffi.bind ffi.call") == TOY_OK,
          "call mixed numeric function");
    double floating = 0.0;
    CHECK(toy_get_float(state, 0, &floating) && floating == 10.5,
          "floating result");
    CHECK(toy_pop(state, 1), "pop floating result");

    CHECK(toy_push_string(state, "hello", 5) == TOY_OK,
          "push string argument");
    CHECK(push_path(state, argv[2]) == TOY_OK, "push string library path");
    CHECK(toy_eval(state, "<ffi-string-argument>",
                   "ffi.open \"toy_ffi_text_length\" "
                   "\"cstr -> usize\" ffi.bind ffi.call") == TOY_OK,
          "call C string function");
    CHECK(toy_get_int(state, 0, &integer) && integer == 5,
          "string length result");
    CHECK(toy_pop(state, 1), "pop string length");

    CHECK(push_path(state, argv[2]) == TOY_OK, "push greeting library path");
    CHECK(toy_eval(state, "<ffi-string-result>",
                   "ffi.open \"toy_ffi_greeting\" "
                   "\"-> cstr\" ffi.bind ffi.call") == TOY_OK,
          "copy C string result");
    const char *text = NULL;
    size_t text_length = 0;
    CHECK(toy_get_string(state, 0, &text, &text_length) &&
              text_length == strlen("hello from C") &&
              memcmp(text, "hello from C", text_length) == 0,
          "C string result");
    CHECK(toy_pop(state, 1), "pop C string result");

    CHECK(toy_push_bool(state, true) == TOY_OK, "push bool argument");
    CHECK(push_path(state, argv[2]) == TOY_OK, "push bool library path");
    CHECK(toy_eval(state, "<ffi-bool>",
                   "ffi.open \"toy_ffi_not\" "
                   "\"bool -> bool\" ffi.bind ffi.call") == TOY_OK,
          "call bool function");
    bool boolean = true;
    CHECK(toy_get_bool(state, 0, &boolean) && !boolean, "bool result");
    CHECK(toy_pop(state, 1), "pop bool result");

    CHECK(toy_push_int(state, 7) == TOY_OK, "push void argument");
    CHECK(push_path(state, argv[2]) == TOY_OK, "push void library path");
    CHECK(toy_eval(state, "<ffi-void>",
                   "ffi.open \"toy_ffi_ignore_i32\" "
                   "\"i32 -> void\" ffi.bind ffi.call") == TOY_OK,
          "call void function");
    CHECK(toy_stack_size(state) == 0, "void function result");

    CHECK(push_path(state, argv[2]) == TOY_OK, "push invalid library path");
    CHECK(toy_eval(state, "<ffi-invalid-signature>",
                   "ffi.open \"toy_ffi_add_i32\" "
                   "\"pointer -> i32\" ffi.bind") == TOY_ERROR,
          "reject unsupported signature type");
    CHECK(toy_get_error(state) &&
              strstr(toy_get_error(state), "invalid signature"),
          "invalid signature diagnostic");
    CHECK(toy_pop(state, 3), "pop invalid binding inputs");

    CHECK(push_path(state, argv[2]) == TOY_OK,
          "push obsolete signature library path");
    CHECK(toy_eval(state, "<ffi-obsolete-signature>",
                   "ffi.open \"toy_ffi_add_i32\" "
                   "\"i32(i32 i32)\" ffi.bind") == TOY_ERROR,
          "reject obsolete return-first signature");
    CHECK(toy_get_error(state) &&
              strstr(toy_get_error(state), "invalid signature"),
          "obsolete signature diagnostic");
    CHECK(toy_pop(state, 3), "pop obsolete binding inputs");

    CHECK(push_path(state, argv[2]) == TOY_OK, "push missing symbol path");
    CHECK(toy_eval(state, "<ffi-missing-symbol>",
                   "ffi.open \"toy_ffi_missing\" "
                   "\"-> void\" ffi.bind") == TOY_ERROR,
          "reject missing symbol");
    CHECK(toy_get_error(state) &&
              strstr(toy_get_error(state), "could not resolve"),
          "missing symbol diagnostic");
    CHECK(toy_pop(state, 3), "pop missing symbol inputs");

    CHECK(toy_push_int(state, 40000) == TOY_OK, "push range argument");
    CHECK(push_path(state, argv[2]) == TOY_OK, "push range library path");
    CHECK(toy_eval(state, "<ffi-range>",
                   "ffi.open \"toy_ffi_ignore_i32\" "
                   "\"i16 -> void\" ffi.bind ffi.call") == TOY_ERROR,
          "reject out-of-range argument");
    CHECK(toy_get_error(state) && strstr(toy_get_error(state), "expected i16"),
          "argument range diagnostic");
    CHECK(toy_pop(state, 2), "pop rejected call inputs");

    CHECK(push_path(state, argv[2]) == TOY_OK, "push u64 library path");
    CHECK(toy_eval(state, "<ffi-return-range>",
                   "ffi.open \"toy_ffi_too_large\" "
                   "\"-> u64\" ffi.bind ffi.call") == TOY_ERROR,
          "reject unrepresentable return value");
    CHECK(toy_get_error(state) &&
              strstr(toy_get_error(state), "outside Toy's range"),
          "return range diagnostic");
    CHECK(toy_stack_size(state) == 0, "failed return consumes call inputs");

    CHECK(push_path(state, argv[2]) == TOY_OK, "push shutdown library path");
    CHECK(toy_eval(state, "<ffi-shutdown>",
                   "ffi.open \"toy_ffi_add_i32\" "
                   "\"i32 i32 -> i32\" ffi.bind") == TOY_OK,
          "leave bound function for state teardown");
    toy_state_free(state);

#ifdef STB_LEAKCHECK
    stb_leakcheck_dumpmem();
#endif
    return 0;
}

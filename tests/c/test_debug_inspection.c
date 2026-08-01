#include "tf_exec.h"

#include <stdio.h>
#include <string.h>

#include "tf_builtins.h"
#include "tf_obj.h"
#include "tf_parser.h"

#ifdef STB_LEAKCHECK
#include "tf_alloc.h"
#endif

#define CHECK(condition, message)                                               \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "debug inspection check failed: %s\n", message); \
            return 1;                                                           \
        }                                                                       \
    } while (0)

typedef struct {
    const char *failure;
    bool inspected;
} inspection_state;

static bool capture_is(tf_debug_capture_info *capture, const char *name,
                       int64_t value) {
    return strcmp(capture->name, name) == 0 &&
           tf_obj_typeof(capture->value) == TF_OBJ_TYPE_INT &&
           tf_obj_int_value(capture->value) == value;
}

static tf_debug_action inspect_hook(tf_ctx *ctx,
                                    const tf_debug_event *event,
                                    void *userdata) {
    inspection_state *state = userdata;
    if (state->inspected || event->instruction->type != TF_OBJ_TYPE_VARFETCH ||
        strcmp(event->instruction->str.ptr, "outer") != 0) {
        return TF_DEBUG_STEP;
    }

    tf_debug_capture_info capture;
    tf_debug_word_info word;
    if (tf_debug_frame_count(ctx) != 2) {
        state->failure = "active frame count";
    } else if (tf_debug_capture_count(ctx, 0) != 1 ||
               !tf_debug_get_capture(ctx, 0, 0, &capture) ||
               !capture_is(&capture, "inner", 20)) {
        state->failure = "current frame capture";
    } else if (tf_debug_capture_count(ctx, 1) != 1 ||
               !tf_debug_get_capture(ctx, 1, 0, &capture) ||
               !capture_is(&capture, "outer", 10)) {
        state->failure = "caller frame capture";
    } else if (!tf_debug_lookup_capture(ctx, "outer", 5, &capture) ||
               !capture_is(&capture, "outer", 10)) {
        state->failure = "dynamic capture lookup";
    } else if (!tf_debug_find_word(ctx, "inspect", 7, &word) ||
               !word.user_defined || !word.body ||
               word.body->type != TF_OBJ_TYPE_VECTOR) {
        state->failure = "user word lookup";
    } else if (!tf_debug_find_word(ctx, "+", 1, &word) ||
               word.user_defined || word.body != NULL) {
        state->failure = "native word lookup";
    } else {
        size_t word_count = tf_debug_word_count(ctx);
        bool found = false;
        for (size_t i = 0; i < word_count; i++) {
            if (tf_debug_get_word(ctx, i, &word) && word.user_defined &&
                strcmp(word.name, "inspect") == 0) {
                found = true;
                break;
            }
        }
        if (!found) state->failure = "user word enumeration";
    }
    state->inspected = true;
    return TF_DEBUG_STEP;
}

int main(void) {
    tf_ctx *ctx = tf_ctx_new(0, NULL);
    CHECK(ctx != NULL, "context creation");

    const char *source =
        "'inspect [ 20 | inner | $outer $inner + ] def "
        "10 | outer | inspect";
    tf_obj *program = tf_parse_source(NULL, "<debug-inspection>", source);
    CHECK(program != NULL, "source parsing");

    inspection_state state = {0};
    tf_debug_set_hook(ctx, inspect_hook, &state);
    CHECK(tf_vm_exec(ctx, program) == TF_OK, "program execution");
    CHECK(state.inspected && state.failure == NULL,
          state.failure ? state.failure : "hook inspection");
    CHECK(tf_stack_len(ctx) == 1 &&
              tf_obj_typeof(tf_stack_peek(ctx, 0)) == TF_OBJ_TYPE_INT &&
              tf_obj_int_value(tf_stack_peek(ctx, 0)) == 30,
          "program result");

    tf_debug_set_hook(ctx, NULL, NULL);
    tf_obj_release(program);
    tf_ctx_free(ctx);
#ifdef STB_LEAKCHECK
    stb_leakcheck_dumpmem();
#endif
    return 0;
}

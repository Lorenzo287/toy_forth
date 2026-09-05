#include "tf_exec.h"

#include <stdio.h>
#include <string.h>

#ifdef STB_LEAKCHECK
#include "tf_alloc.h"
#endif

#define CHECK(condition, message)                                            \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "capture storage check failed: %s\n", message); \
            return 1;                                                       \
        }                                                                   \
    } while (0)

typedef struct {
    bool abort;
    size_t inspections;
    const char *failure;
} inspection;

static toy_state *other_state;
static int destroyed[3];
static size_t destroyed_len;

static void discard(void *userdata, const char *text, size_t length) {
    (void)userdata;
    (void)text;
    (void)length;
}

static bool capture_value(tf_ctx *ctx, const char *name, int64_t expected) {
    tf_debug_capture_info info;
    return tf_debug_lookup_capture(ctx, name, strlen(name), &info) &&
           tf_obj_typeof(info.value) == TF_OBJ_TYPE_INT &&
           tf_obj_int_value(info.value) == expected;
}

static tf_debug_action inspect(tf_ctx *ctx, const tf_debug_event *event,
                               void *userdata) {
    inspection *state = userdata;
    if (tf_obj_typeof(event->instruction) != TF_OBJ_TYPE_CALL ||
        strcmp(event->instruction->str.ptr, "capture-marker") != 0) {
        return TF_DEBUG_STEP;
    }
    state->inspections++;
    if (!capture_value(ctx, "root", 101) || !capture_value(ctx, "n", 0) ||
        !capture_value(ctx, "a", 7) || !capture_value(ctx, "c", 9)) {
        state->failure = "lookup across relocated frames and bindings";
    }
    size_t total = 0;
    for (size_t depth = 0; depth < tf_debug_frame_count(ctx); depth++) {
        size_t count = tf_debug_capture_count(ctx, depth);
        for (size_t i = 0; i < count; i++) {
            tf_debug_capture_info info;
            if (!tf_debug_get_capture(ctx, depth, i, &info) ||
                !info.name || tf_obj_typeof(info.value) != TF_OBJ_TYPE_INT) {
                state->failure = "per-frame capture inspection after growth";
            }
        }
        total += count;
    }
    if (total != 165 || ctx->captures_len != total) {
        state->failure = "active capture ownership";
    }
    return state->abort ? TF_DEBUG_ABORT : TF_DEBUG_STEP;
}

static toy_status marker(toy_state *state) {
    if (toy_eval(other_state, "<other-captures>",
                 "202 | root | $root") != TOY_OK) return TOY_ERROR;
    int64_t result = 0;
    bool ok = toy_get_int(other_state, 0, &result) && result == 202 &&
              other_state->captures_len == 0 && capture_value(state, "root", 101);
    toy_pop(other_state, toy_stack_size(other_state));
    return ok ? TOY_OK : TOY_ERROR;
}

static toy_status stop_now(toy_state *state) {
    toy_interrupt(state);
    return TOY_OK;
}

static void destroy_resource(void *resource, void *userdata) {
    (void)userdata;
    if (destroyed_len < sizeof(destroyed) / sizeof(destroyed[0])) {
        destroyed[destroyed_len++] = *(int *)resource;
    }
}

int main(void) {
    toy_state_config config = {.diagnostic = discard};
    toy_state *state = toy_state_new(&config);
    other_state = toy_state_new(&config);
    CHECK(state && other_state, "create independent contexts");
    CHECK(toy_register_word(state, "capture-marker", marker) == TOY_OK &&
              toy_register_word(state, "stop-now", stop_now) == TOY_OK,
          "register capture callbacks");

    const char *source =
        "101 | root | 7 8 9 | a b c | "
        "'descend [ | n | $n 0 == [ capture-marker ] "
        "[ $n 1 - descend ] ifelse ] def 160 descend $root";
    inspection probe = {0};
    tf_debug_set_hook(state, inspect, &probe);
    size_t capacity = 0;
    for (size_t run = 0; run < 3; run++) {
        CHECK(toy_eval(state, "<capture-growth>", source) == TOY_OK,
              "deep scopes and nested independent context");
        CHECK(probe.inspections == run + 1 && !probe.failure,
              probe.failure ? probe.failure : "debugger inspected each run");
        CHECK(state->captures_len == 0 && state->call_stack_len == 0,
              "normal completion releases active bindings");
        CHECK(!capture_value(state, "root", 101), "finished scope is not visible");
        int64_t result = 0;
        CHECK(toy_stack_size(state) == 1 && toy_get_int(state, 0, &result) &&
                  result == 101 && toy_pop(state, 1), "outer value survives growth");
        if (run == 0) capacity = state->captures_cap;
        CHECK(capacity >= 165 && state->captures_cap == capacity,
              "later calls reuse capture capacity");
    }

    probe.abort = true;
    CHECK(toy_eval(state, "<capture-abort>", source) == TOY_INTERRUPTED &&
              !probe.failure && state->captures_len == 0 &&
              state->call_stack_len == 0, "debug abort unwinds all captures");
    tf_debug_set_hook(state, NULL, NULL);
    CHECK(toy_eval(state, "<capture-interrupt>",
                   "[ 1 2 3 | a b c | stop-now 999 ] exec") == TOY_INTERRUPTED &&
              state->captures_len == 0 && state->call_stack_len == 0 &&
              toy_stack_size(state) == 0, "native interruption unwinds captures");
    CHECK(toy_eval(state, "<capture-error>",
                   "1 | outer | [ 2 3 4 | a b c | missing-word ] exec") == TOY_ERROR &&
              state->captures_len == 0 && state->call_stack_len == 0,
          "unhandled errors unwind captures");
    CHECK(toy_eval(state, "<capture-exit>",
                   "1 | outer | [ 2 | inner | 0 exit ] exec") == TOY_EXIT_REQUESTED &&
              state->captures_len == 0 && state->call_stack_len == 0,
          "exit requests unwind captures");
    CHECK(toy_eval(state, "<capture-reuse>", "5 | fresh | $fresh") == TOY_OK &&
              state->captures_len == 0 && toy_pop(state, 1),
          "context remains reusable after every unwind path");

    /* Binding order is right-to-left; retain the previous forward cleanup
     * order within each frame, including observable resource destructors. */
    int ids[] = {1, 2, 3};
    for (size_t i = 0; i < 3; i++) {
        CHECK(toy_push_resource(state, "capture-test", &ids[i],
                                 destroy_resource, NULL) == TOY_OK,
              "create capture-owned resources");
    }
    CHECK(toy_eval(state, "<capture-resources>", "| a b c |") == TOY_OK &&
              destroyed_len == 3 && destroyed[0] == 3 && destroyed[1] == 2 &&
              destroyed[2] == 1, "capture cleanup preserves destructor order");
    toy_state_free(other_state);
    toy_state_free(state);
#ifdef STB_LEAKCHECK
    stb_leakcheck_dumpmem();
#endif
    return 0;
}

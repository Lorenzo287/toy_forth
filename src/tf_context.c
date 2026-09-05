#include "tf_exec.h"

#include <stdlib.h>

#include "tf_alloc.h"
#include "tf_builtins.h"  // IWYU pragma: keep
#include "tf_native_loader.h"

/* === Context Initialization === */

static void register_builtin_group(tf_ctx *ctx, const tf_builtin_group *group) {
    for (size_t i = 0; group->words[i].name; i++) {
        tf_dict_set_native(ctx, group->words[i].name, group->words[i].cb);
    }
}

#include "generated/tf_builtins.inc"

static size_t builtin_word_count(void) {
    size_t count = 0;
    size_t group_count =
        sizeof(native_builtin_groups) / sizeof(native_builtin_groups[0]);
    for (size_t i = 0; i < group_count; i++) {
        for (size_t j = 0; native_builtin_groups[i].words[j].name; j++) count++;
    }
    return count;
}

const tf_builtin_group *tf_builtin_groups(size_t *count) {
    if (count) {
        *count = sizeof(native_builtin_groups) / sizeof(native_builtin_groups[0]);
    }
    return native_builtin_groups;
}

tf_ctx *tf_ctx_new(int argc, char **argv) {
    tf_ctx *ctx = tf_xcalloc(1, sizeof(*ctx));
    tf_obj_pool_init(&ctx->objects);
    tf_obj_pool *previous_pool = tf_obj_pool_enter(&ctx->objects);
    tf_random_init(&ctx->random, ctx);
    ctx->data_stack = tf_obj_new_vector();
    tf_dict_init(ctx, builtin_word_count());
    tf_packages_init(ctx);
    ctx->argc = argc;
    ctx->argv = argv;
    tf_ctx_set_output(ctx, NULL, NULL);
    tf_ctx_set_diagnostic(ctx, NULL, NULL);

    size_t group_count = 0;
    const tf_builtin_group *groups = tf_builtin_groups(&group_count);
    for (size_t i = 0; i < group_count; i++) {
        register_builtin_group(ctx, &groups[i]);
    }

    tf_obj_pool_leave(previous_pool);
    return ctx;
}

void tf_ctx_free(tf_ctx *ctx) {
    tf_obj_pool *previous_pool = tf_obj_pool_enter(&ctx->objects);
    tf_deferred_calls_clear(ctx);
    tf_obj_release(ctx->data_stack);
    while (ctx->call_stack_len > 0) tf_frame_pop(ctx, TF_OK);
    free(ctx->call_stack);
    free(ctx->captures);
    tf_scratch_clear(ctx);
    tf_control_state_cache_clear(ctx);
    tf_quick_program_cache_clear(ctx);
    tf_dict_free(ctx);
    tf_packages_free(ctx);
    free(ctx->last_error);
    tf_native_packages_close(ctx);
    tf_obj_pool_clear(&ctx->objects);
    tf_obj_pool_leave(previous_pool);
    free(ctx);
}

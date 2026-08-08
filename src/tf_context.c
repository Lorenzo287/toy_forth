#include "tf_exec.h"

#include <stdlib.h>
#include <string.h>

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

static size_t word_table_capacity_for(size_t count) {
    size_t capacity = 8;
    while (count >= capacity * 7 / 10) capacity *= 2;
    return capacity;
}

const tf_builtin_group *tf_builtin_groups(size_t *count) {
    if (count) {
        *count = sizeof(native_builtin_groups) / sizeof(native_builtin_groups[0]);
    }
    return native_builtin_groups;
}

tf_ctx *tf_ctx_new(int argc, char **argv) {
    tf_ctx *ctx = tf_xmalloc(sizeof(tf_ctx));
    tf_obj_pool_init(&ctx->objects);
    tf_obj_pool *previous_pool = tf_obj_pool_enter(&ctx->objects);
    tf_random_init(&ctx->random, ctx);
    ctx->data_stack = tf_obj_new_vector();
    size_t builtin_count = builtin_word_count();
    ctx->words.entry_capacity = builtin_count + 16;
    ctx->words.entries =
        tf_xmalloc(sizeof(tf_word) * ctx->words.entry_capacity);
    ctx->words.capacity = word_table_capacity_for(builtin_count);
    ctx->words.count = 0;
    ctx->words.buckets = tf_xcalloc(ctx->words.capacity, sizeof(size_t));
    ctx->words.resolution_generation = 1;
    memset(ctx->words.lookup_cache, 0, sizeof(ctx->words.lookup_cache));
    memset(ctx->quick_programs, 0, sizeof(ctx->quick_programs));
    ctx->call_stack = NULL;
    ctx->call_stack_len = 0;
    ctx->call_stack_cap = 0;
    ctx->deferred_head = NULL;
    ctx->deferred_tail = NULL;
    ctx->deferred_count = 0;
    ctx->deferred_active = false;
    ctx->scratch = (tf_scratch_arena){0};
    ctx->control_state_cache = NULL;
    ctx->control_state_cache_len = 0;
    ctx->packages.cap = 4;
    ctx->packages.len = 1;
    ctx->packages.entries = tf_xcalloc(ctx->packages.cap, sizeof(tf_package));
    ctx->packages.entries[TF_ROOT_PACKAGE].name = tf_xstrdup("");
    ctx->packages.entries[TF_ROOT_PACKAGE].name_len = 0;
    ctx->packages.entries[TF_ROOT_PACKAGE].path = NULL;
    ctx->packages.entries[TF_ROOT_PACKAGE].state = TF_PACKAGE_LOADED;
    ctx->package_imports.cap = 4;
    ctx->package_imports.len = 0;
    ctx->package_imports.entries =
        tf_xcalloc(ctx->package_imports.cap, sizeof(tf_package_import));
    ctx->native_libraries.handles = NULL;
    ctx->native_libraries.len = 0;
    ctx->native_libraries.cap = 0;
    ctx->core_package_path = NULL;
    ctx->argc = argc;
    ctx->argv = argv;
    ctx->error_suppression_depth = 0;
    ctx->error_reported = false;
    ctx->program_error = false;
    ctx->suppress_repl_status = false;
    ctx->interrupted = 0;
    ctx->last_error = NULL;
    ctx->current_span = (tf_source_span){0};
    ctx->current_word = NULL;
    ctx->debug_hook = NULL;
    ctx->debug_userdata = NULL;
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
    tf_scratch_clear(ctx);
    tf_control_state_cache_clear(ctx);
    tf_quick_program_cache_clear(ctx);
    tf_dict_lookup_cache_clear(ctx);
    for (size_t i = 0; i < ctx->words.count; i++) {
        tf_word *word = &ctx->words.entries[i];
        if (word->owns_name) free((char *)word->name);
        free(word->doc_stack_effect);
        free(word->doc_description);
        if (word->type == TF_WORD_USER) tf_obj_release(word->user_impl);
    }
    free(ctx->words.entries);
    free(ctx->words.buckets);
    for (size_t i = 0; i < ctx->packages.len; i++) {
        free(ctx->packages.entries[i].name);
        free(ctx->packages.entries[i].path);
    }
    free(ctx->packages.entries);
    for (size_t i = 0; i < ctx->package_imports.len; i++) {
        free(ctx->package_imports.entries[i].name);
    }
    free(ctx->package_imports.entries);
    free(ctx->core_package_path);
    free(ctx->last_error);
    tf_native_packages_close(ctx);
    tf_obj_pool_clear(&ctx->objects);
    tf_obj_pool_leave(previous_pool);
    free(ctx);
}

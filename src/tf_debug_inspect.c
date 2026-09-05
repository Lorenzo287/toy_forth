#include "tf_exec.h"

#include <string.h>

static bool frame_is_program(tf_frame_kind kind) {
    return kind != TF_FRAME_NATIVE;
}

void tf_debug_set_hook(tf_ctx *ctx, tf_debug_hook_fn hook, void *userdata) {
    ctx->debug_hook = hook;
    ctx->debug_userdata = userdata;
}

size_t tf_debug_frame_count(tf_ctx *ctx) {
    return ctx->call_stack_len;
}

static const char *debug_user_word_name(tf_ctx *ctx, size_t frame_index,
                                        tf_frame *frame) {
    if (frame_index > 0) {
        tf_frame *caller = &ctx->call_stack[frame_index - 1];
        if (frame_is_program(caller->kind) && caller->as.program.pc > 0) {
            tf_obj *instruction = caller->as.program.program->vector.elem[
                caller->as.program.pc - 1];
            if (tf_obj_typeof(instruction) == TF_OBJ_TYPE_CALL) {
                return instruction->str.ptr;
            }
        }
    }
    for (size_t i = 0; i < ctx->words.count; i++) {
        tf_word *word = &ctx->words.entries[i];
        if (word->type == TF_WORD_USER &&
            word->user_impl == frame->as.program.program) {
            return word->name;
        }
    }
    return "<user word>";
}

bool tf_debug_get_frame(tf_ctx *ctx, size_t depth,
                        tf_debug_frame_info *info) {
    if (!info || depth >= ctx->call_stack_len) return false;
    size_t frame_index = ctx->call_stack_len - 1 - depth;
    tf_frame *frame = &ctx->call_stack[frame_index];
    info->kind = frame_is_program(frame->kind) ? TF_FRAME_PROGRAM
                                               : TF_FRAME_NATIVE;
    info->call_site = frame->call_site;
    info->location = frame->call_site;
    info->word_name = NULL;
    info->pc = 0;
    info->program_len = 0;
    if (frame_is_program(frame->kind)) {
        if (frame->kind == TF_FRAME_PROGRAM_ROOT) {
            info->word_name = "<program>";
        } else if (frame->kind == TF_FRAME_PROGRAM_USER) {
            info->word_name = debug_user_word_name(ctx, frame_index, frame);
        } else {
            info->word_name = "<quotation>";
        }
        info->pc = frame->as.program.pc;
        info->program_len = frame->as.program.program->vector.len;
        if (depth == 0 && info->pc < info->program_len) {
            info->location = tf_obj_span(
                frame->as.program.program->vector.elem[info->pc]);
        } else if (info->pc > 0) {
            info->location = tf_obj_span(
                frame->as.program.program->vector.elem[info->pc - 1]);
        }
    }
    return true;
}

static tf_var_table *debug_frame_vars(tf_ctx *ctx, size_t depth) {
    if (depth >= ctx->call_stack_len) return NULL;
    size_t frame_index = ctx->call_stack_len - 1 - depth;
    tf_frame *frame = &ctx->call_stack[frame_index];
    if (!frame_is_program(frame->kind)) return NULL;
    return &frame->as.program.vars;
}

size_t tf_debug_capture_count(tf_ctx *ctx, size_t frame_depth) {
    tf_var_table *vars = debug_frame_vars(ctx, frame_depth);
    return vars ? vars->len : 0;
}

bool tf_debug_get_capture(tf_ctx *ctx, size_t frame_depth, size_t index,
                          tf_debug_capture_info *info) {
    tf_var_table *vars = debug_frame_vars(ctx, frame_depth);
    if (!info || !vars || index >= vars->len) return false;
    tf_var *bindings = ctx->captures + vars->base;
    info->name = bindings[index].name->str.ptr;
    info->value = bindings[index].val;
    return true;
}

bool tf_debug_lookup_capture(tf_ctx *ctx, const char *name, size_t name_len,
                             tf_debug_capture_info *info) {
    if (!name || !info) return false;
    for (size_t i = ctx->captures_len; i > 0; i--) {
        tf_var *binding = &ctx->captures[i - 1];
        tf_obj *binding_name = binding->name;
        if (binding_name->str.len == name_len &&
            memcmp(binding_name->str.ptr, name, name_len) == 0) {
            info->name = binding_name->str.ptr;
            info->value = binding->val;
            return true;
        }
    }
    return false;
}

size_t tf_debug_word_count(tf_ctx *ctx) {
    return ctx->words.count;
}

static void debug_fill_word_info(tf_word *word, tf_debug_word_info *info) {
    info->name = word->name;
    info->user_defined = word->type == TF_WORD_USER;
    info->body = info->user_defined ? word->user_impl : NULL;
}

bool tf_debug_get_word(tf_ctx *ctx, size_t index, tf_debug_word_info *info) {
    if (!info || index >= ctx->words.count) return false;
    debug_fill_word_info(&ctx->words.entries[index], info);
    return true;
}

bool tf_debug_find_word(tf_ctx *ctx, const char *name, size_t name_len,
                        tf_debug_word_info *info) {
    if (!name || !info) return false;
    tf_word *word = tf_dict_lookup_scoped(
        ctx, tf_current_package_index(ctx), name, name_len);
    if (!word) return false;
    debug_fill_word_info(word, info);
    return true;
}

#include "tf_exec.h"
#include <stdio.h>
#include <string.h>
#include "tf_alloc.h"
#include "tf_lib.h"

/* wrappers for managing the context stack, based on less abstract object
   manipulation functions defined in tf_obj */
size_t stack_len(tf_ctx *ctx) {
    return ctx->stack->list.len;
}
tf_obj *stack_pop(tf_ctx *ctx) {
    return pop_obj(ctx->stack);
}
tf_obj *stack_pop_type(tf_ctx *ctx, tf_type type) {
    return pop_obj_type(ctx->stack, type);
}
void stack_push(tf_ctx *ctx, tf_obj *o) {
    push_obj(ctx->stack, o);
}

/* === Context Initialization === */

tf_ctx *init_ctx(void) {
    tf_ctx *ctx = xmalloc(sizeof(tf_ctx));
    ctx->stack = init_list_obj();
    ctx->functions = NULL;
    ctx->funcount = 0;
    ctx->curr_prg = NULL;
    ctx->curr_pc = 0;

    set_native_func(ctx, "+", math_functions);
    set_native_func(ctx, "-", math_functions);
    set_native_func(ctx, "*", math_functions);
    set_native_func(ctx, "/", math_functions);
    set_native_func(ctx, "%", math_functions);
    set_native_func(ctx, "mod", math_functions);
    set_native_func(ctx, "abs", math_functions);
    set_native_func(ctx, "max", math_functions);
    set_native_func(ctx, "min", math_functions);

    set_native_func(ctx, "dup", stack_functions);
    set_native_func(ctx, "drop", stack_functions);
    set_native_func(ctx, "swap", stack_functions);
    set_native_func(ctx, "over", stack_functions);
    set_native_func(ctx, "rot", stack_functions);

    set_native_func(ctx, "print", io_functions);
    set_native_func(ctx, "println", io_functions);
    set_native_func(ctx, ".", io_functions);
    set_native_func(ctx, ".s", io_functions);

    set_native_func(ctx, "==", compare_functions);
    set_native_func(ctx, "!=", compare_functions);
    set_native_func(ctx, "<", compare_functions);
    set_native_func(ctx, ">", compare_functions);
    set_native_func(ctx, "<=", compare_functions);
    set_native_func(ctx, ">=", compare_functions);

    set_native_func(ctx, "exec", control_functions);
    set_native_func(ctx, "if", control_functions);
    set_native_func(ctx, "ifelse", control_functions);
    set_native_func(ctx, "times", control_functions);
    set_native_func(ctx, "each", control_functions);
    set_native_func(ctx, "while", control_functions);

    set_native_func(ctx, ":", definition_functions);
    set_native_func(ctx, "def", definition_functions);

    return ctx;
}

tf_func *init_func(tf_ctx *ctx, tf_obj *name) {
    ctx->functions =
        xrealloc(ctx->functions, sizeof(tf_func *) * (ctx->funcount + 1));
    tf_func *f = xmalloc(sizeof(tf_func));
    f->name = name;
    retain_obj(name);
    f->type = TF_FUNC_TYPE_NATIVE;
    f->native_impl = NULL;
    ctx->functions[ctx->funcount++] = f;
    return f;
}

void set_native_func(tf_ctx *ctx, char *name, tf_cb cb) {
    tf_obj *o_name = create_string_obj(name, strlen(name));
    tf_func *f = get_func(ctx, o_name);
    if (f) {  // overwrite if name is already taken
        if (f->type == TF_FUNC_TYPE_USER) release_obj(f->user_impl);
    } else {  // allocate if name is not taken
        f = init_func(ctx, o_name);
    }
    f->type = TF_FUNC_TYPE_NATIVE;
    f->native_impl = cb;
    release_obj(o_name);
}

void set_user_func(tf_ctx *ctx, tf_obj *name, tf_obj *uf) {
    tf_func *f = get_func(ctx, name);
    if (f) {
        if (f->type == TF_FUNC_TYPE_USER) { release_obj(f->user_impl); }
    } else {
        f = init_func(ctx, name);
    }
    f->type = TF_FUNC_TYPE_USER;
    f->user_impl = uf;
    retain_obj(uf);
}

// FIX: O(n) scan to find function, could implement with hash map?
tf_func *get_func(tf_ctx *ctx, tf_obj *name) {
    for (size_t i = 0; i < ctx->funcount; i++) {
        tf_func *f = ctx->functions[i];
        if (compare_string_obj(f->name, name) == 0) return f;
    }
    return NULL;
}

/* === Execution === */

int exec(tf_ctx *ctx, tf_obj *prg) {
    if (prg->type != TF_OBJ_TYPE_LIST) return TF_ERR;

    tf_obj *old_prg = ctx->curr_prg;
    size_t old_pc = ctx->curr_pc;

    ctx->curr_prg = prg;
    for (ctx->curr_pc = 0; ctx->curr_pc < prg->list.len; ctx->curr_pc++) {
        tf_obj *o = prg->list.elem[ctx->curr_pc];
        switch (o->type) {
        case TF_OBJ_TYPE_SYMBOL:
            if (o->str.quoted) {
                stack_push(ctx, o);
                retain_obj(o);
            } else if (call_symbol(ctx, o) == TF_ERR) {
                printf("Run time error\n");
                return TF_ERR;
            }
            break;
        default:
            stack_push(ctx, o);
            retain_obj(o);
            break;
        }
    }

    ctx->curr_prg = old_prg;
    ctx->curr_pc = old_pc;
    return TF_OK;
}

int call_symbol(tf_ctx *ctx, tf_obj *symb) {
    tf_func *f = get_func(ctx, symb);
    if (!f) return TF_ERR;
    if (f->type == TF_FUNC_TYPE_USER) {
        return exec(ctx, f->user_impl);
    } else {
        return f->native_impl(ctx, f->name->str.ptr);
    }
}

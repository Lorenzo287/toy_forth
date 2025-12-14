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
tf_obj *stack_pop(tf_ctx *ctx, tf_type type) {
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

    set_c_func(ctx, "+", math_functions);
    set_c_func(ctx, "-", math_functions);
    set_c_func(ctx, "*", math_functions);
    set_c_func(ctx, "/", math_functions);

    // set_user_func();
    // WARN: how can this be set here if they are defined at runtime?
    return ctx;
}

tf_func *init_func(tf_ctx *ctx, tf_obj *name) {
    ctx->functions =
        xrealloc(ctx->functions, sizeof(tf_func *) * (ctx->funcount + 1));
    tf_func *f = xmalloc(sizeof(tf_func));
    f->name = name;
    retain_obj(name);
    f->callback = NULL;
    f->userfunc = NULL;
    ctx->functions[ctx->funcount++] = f;
    return f;
}

void set_c_func(tf_ctx *ctx, char *name, tf_cb cb) {
    tf_obj *o_name = create_string_obj(name, strlen(name));
    tf_func *f = get_func(ctx, o_name);
    if (f) {  // overwrite if already defined
        if (f->userfunc) {
            release_obj(f->userfunc);
            f->userfunc = NULL;
        }
        f->callback = cb;
    } else {
        f = init_func(ctx, o_name);
        f->callback = cb;
    }
    release_obj(o_name);
}

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
    for (size_t i = 0; i < prg->list.len; i++) {
        tf_obj *o = prg->list.elem[i];
        switch (o->type) {
        case TF_OBJ_TYPE_SYMBOL:
            if (call_symbol(ctx, o) == TF_ERR) {
                printf("Run time error\n");
                return TF_ERR;
            }
            break;
        default:
            push_obj(ctx->stack, o);
            retain_obj(o);
            break;
        }
    }
    return TF_OK;
}

int call_symbol(tf_ctx *ctx, tf_obj *symb) {
    tf_func *f = get_func(ctx, symb);
    if (!f) return TF_ERR;
    if (f->userfunc) {
        // TODO: exec()
        return TF_ERR;
    } else {
        return f->callback(ctx, f->name->str.ptr);
    }
}

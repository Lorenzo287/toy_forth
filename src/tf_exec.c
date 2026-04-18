#include "tf_exec.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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

/* === Function Table Helpers === */

static unsigned long tf_hash(tf_obj *o) {
    unsigned long hash = 5381;
    char *ptr = o->str.ptr;
    size_t len = o->str.len;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + ptr[i];
    }
    return hash;
}

static void tf_table_resize(tf_ctx *ctx) {
    size_t old_cap = ctx->functions.capacity;
    tf_func **old_buckets = ctx->functions.buckets;

    ctx->functions.capacity *= 2;
    ctx->functions.buckets = xcalloc(ctx->functions.capacity, sizeof(tf_func *));

    for (size_t i = 0; i < old_cap; i++) {
        tf_func *f = old_buckets[i];
        if (f) {
            unsigned long h = tf_hash(f->name);
            size_t idx = h % ctx->functions.capacity;
            while (ctx->functions.buckets[idx]) {
                idx = (idx + 1) % ctx->functions.capacity;
            }
            ctx->functions.buckets[idx] = f;
        }
    }
    free(old_buckets);
}

/* === Context Initialization === */

tf_ctx *init_ctx(void) {
    tf_ctx *ctx = xmalloc(sizeof(tf_ctx));
    ctx->stack = init_list_obj();
    ctx->functions.capacity = 16;
    ctx->functions.count = 0;
    ctx->functions.buckets = xcalloc(ctx->functions.capacity, sizeof(tf_func *));
    ctx->curr_prg = NULL;
    ctx->curr_pc = 0;

    set_native_func(ctx, "+", tf_add);
    set_native_func(ctx, "-", tf_sub);
    set_native_func(ctx, "*", tf_mul);
    set_native_func(ctx, "/", tf_div);
    set_native_func(ctx, "%", tf_mod);
    set_native_func(ctx, "mod", tf_mod);
    set_native_func(ctx, "abs", tf_abs);
    set_native_func(ctx, "max", tf_max);
    set_native_func(ctx, "min", tf_min);

    set_native_func(ctx, "dup", tf_dup);
    set_native_func(ctx, "drop", tf_drop);
    set_native_func(ctx, "swap", tf_swap);
    set_native_func(ctx, "over", tf_over);
    set_native_func(ctx, "rot", tf_rot);

    set_native_func(ctx, "print", tf_print);
    set_native_func(ctx, "println", tf_println);
    set_native_func(ctx, ".", tf_dot);
    set_native_func(ctx, ".s", tf_stack);

    set_native_func(ctx, "==", tf_eq);
    set_native_func(ctx, "!=", tf_ne);
    set_native_func(ctx, "<", tf_lt);
    set_native_func(ctx, ">", tf_gt);
    set_native_func(ctx, "<=", tf_le);
    set_native_func(ctx, ">=", tf_ge);

    set_native_func(ctx, "exec", tf_exec);
    set_native_func(ctx, "if", tf_if);
    set_native_func(ctx, "ifelse", tf_ifelse);
    set_native_func(ctx, "times", tf_times);
    set_native_func(ctx, "each", tf_each);
    set_native_func(ctx, "while", tf_while);

    set_native_func(ctx, ":", tf_colon);
    set_native_func(ctx, "def", tf_def);

    return ctx;
}

void free_ctx(tf_ctx *ctx) {
    release_obj(ctx->stack);
    for (size_t i = 0; i < ctx->functions.capacity; i++) {
        tf_func *f = ctx->functions.buckets[i];
        if (f) {
            release_obj(f->name);
            if (f->type == TF_FUNC_TYPE_USER) {
                release_obj(f->user_impl);
            }
            free(f);
        }
    }
    free(ctx->functions.buckets);
    free(ctx);
}

tf_func *init_func(tf_ctx *ctx, tf_obj *name) {
    if (ctx->functions.count >= ctx->functions.capacity * 0.7) {
        tf_table_resize(ctx);
    }

    unsigned long h = tf_hash(name);
    size_t idx = h % ctx->functions.capacity;
    while (ctx->functions.buckets[idx]) {
        idx = (idx + 1) % ctx->functions.capacity;
    }

    tf_func *f = xmalloc(sizeof(tf_func));
    f->name = name;
    retain_obj(name);
    f->type = TF_FUNC_TYPE_NATIVE;
    f->native_impl = NULL;
    ctx->functions.buckets[idx] = f;
    ctx->functions.count++;
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

tf_func *get_func(tf_ctx *ctx, tf_obj *name) {
    if (ctx->functions.capacity == 0) return NULL;
    unsigned long h = tf_hash(name);
    size_t idx = h % ctx->functions.capacity;
	// linear probing
    while (ctx->functions.buckets[idx]) {
        if (compare_string_obj(ctx->functions.buckets[idx]->name, name) == 0) {
            return ctx->functions.buckets[idx];
        }
        idx = (idx + 1) % ctx->functions.capacity;
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
        return f->native_impl(ctx);
    }
}

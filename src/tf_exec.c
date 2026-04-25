#include "tf_exec.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "tf_alloc.h"
#include "tf_console.h"
#include "tf_lib.h"
#include <signal.h>

/* === Context Manipulation Helpers === */

/* wrappers for managing the context forth stack, based on less abstract object
   manipulation functions defined in tf_obj */
size_t fstack_len(tf_ctx *ctx) {
    return ctx->forth_stack->list.len;
}
void fstack_push(tf_ctx *ctx, tf_obj *o) {
    push_obj(ctx->forth_stack, o);
}
tf_obj *fstack_pop(tf_ctx *ctx) {
    return pop_obj(ctx->forth_stack);
}
tf_obj *fstack_pop_type(tf_ctx *ctx, tf_type type) {
    return pop_obj_type(ctx->forth_stack, type);
}

/* helpers for managing the context call stack */
void cstack_push(tf_ctx *ctx, tf_obj *prg) {
    ctx->call_stack =
        xrealloc(ctx->call_stack, sizeof(tf_frame) * (ctx->cstack_len + 1));
    ctx->call_stack[ctx->cstack_len].prg = prg;
    ctx->call_stack[ctx->cstack_len].pc = 0;
    ctx->call_stack[ctx->cstack_len].vars.vars = NULL;
    ctx->call_stack[ctx->cstack_len].vars.len = 0;
    ctx->call_stack[ctx->cstack_len].vars.cap = 0;
    retain_obj(prg);
    ctx->cstack_len++;
}

void cstack_pop(tf_ctx *ctx) {
    if (ctx->cstack_len == 0) return;
    tf_frame *f = &ctx->call_stack[ctx->cstack_len - 1];

    for (size_t i = 0; i < f->vars.len; i++) {
        release_obj(f->vars.vars[i].name);
        release_obj(f->vars.vars[i].val);
    }
    free(f->vars.vars);

    release_obj(f->prg);
    ctx->cstack_len--;
}

/* === Function Table Helpers === */

static unsigned long tf_hash(tf_obj *o) {
    unsigned long hash = 5381;
    char *ptr = o->str.ptr;
    size_t len = o->str.len;
    for (size_t i = 0; i < len; i++) { hash = ((hash << 5) + hash) + ptr[i]; }
    return hash;
}

static void tf_table_resize(tf_ctx *ctx) {
    size_t old_cap = ctx->functions.capacity;
    tf_func **old_buckets = ctx->functions.buckets;

    ctx->functions.capacity *= 2;
    ctx->functions.buckets =
        xcalloc(ctx->functions.capacity, sizeof(tf_func *));

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
    srand(time(NULL));
    tf_ctx *ctx = xmalloc(sizeof(tf_ctx));
    ctx->forth_stack = init_list_obj();
    ctx->functions.capacity = 16;
    ctx->functions.count = 0;
    ctx->functions.buckets =
        xcalloc(ctx->functions.capacity, sizeof(tf_func *));
    ctx->call_stack = NULL;
    ctx->cstack_len = 0;

    set_native_func(ctx, "+", tf_add);
    set_native_func(ctx, "-", tf_sub);
    set_native_func(ctx, "*", tf_mul);
    set_native_func(ctx, "/", tf_div);
    set_native_func(ctx, "%", tf_mod);
    set_native_func(ctx, "neg", tf_neg);
    set_native_func(ctx, "mod", tf_mod);
    set_native_func(ctx, "abs", tf_abs);
    set_native_func(ctx, "max", tf_max);
    set_native_func(ctx, "min", tf_min);

    set_native_func(ctx, "dup", tf_dup);
    set_native_func(ctx, "drop", tf_drop);
    set_native_func(ctx, "swap", tf_swap);
    set_native_func(ctx, "over", tf_over);
    set_native_func(ctx, "rot", tf_rot);

    set_native_func(ctx, "printf", tf_printf);
    set_native_func(ctx, "print", tf_print);
    set_native_func(ctx, ".", tf_print);
    set_native_func(ctx, ".s", tf_stack);

    set_native_func(ctx, "==", tf_eq);
    set_native_func(ctx, "!=", tf_ne);
    set_native_func(ctx, "<", tf_lt);
    set_native_func(ctx, ">", tf_gt);
    set_native_func(ctx, "<=", tf_le);
    set_native_func(ctx, ">=", tf_ge);

    set_native_func(ctx, "exec", tf_exec);
    set_native_func(ctx, "if", tf_if_r);
    set_native_func(ctx, "ifelse", tf_ifelse_r);
    set_native_func(ctx, "times", tf_times_r);
    set_native_func(ctx, "each", tf_each_r);
    set_native_func(ctx, "while", tf_while_r);

    set_native_func(ctx, ":", tf_colon);
    set_native_func(ctx, "def", tf_def);

    set_native_func(ctx, "geth", tf_geth);
    set_native_func(ctx, "seth", tf_seth);
    set_native_func(ctx, "len", tf_len);
    set_native_func(ctx, "rand", tf_rand);
    set_native_func(ctx, "sleep", tf_sleep);
    set_native_func(ctx, "key", tf_key);
    set_native_func(ctx, "input", tf_input);
    set_native_func(ctx, "time", tf_time);
    set_native_func(ctx, "clear", tf_clear);
    set_native_func(ctx, "bye", tf_exit);
    set_native_func(ctx, "exit", tf_exit);

    return ctx;
}

void free_ctx(tf_ctx *ctx) {
    release_obj(ctx->forth_stack);
    for (size_t i = 0; i < ctx->functions.capacity; i++) {
        tf_func *f = ctx->functions.buckets[i];
        if (f) {
            release_obj(f->name);
            if (f->type == TF_FUNC_TYPE_USER) { release_obj(f->user_impl); }
            free(f);
        }
    }
    free(ctx->functions.buckets);
    while (ctx->cstack_len > 0) { cstack_pop(ctx); }
    free(ctx->call_stack);
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

/* === Variable Helpers === */

static void tf_var_bind(tf_ctx *ctx, tf_obj *name, tf_obj *val) {
    if (ctx->cstack_len == 0) return;
    tf_frame *f = &ctx->call_stack[ctx->cstack_len - 1];

    // check if variable already exists in current frame and update it
    for (int i = (int)f->vars.len - 1; i >= 0; i--) {
        if (compare_string_obj(f->vars.vars[i].name, name) == 0) {
            release_obj(f->vars.vars[i].val);
            f->vars.vars[i].val = val;
            retain_obj(val);
            return;
        }
    }

    // otherwise append new binding
    if (f->vars.len >= f->vars.cap) {
        f->vars.cap = f->vars.cap == 0 ? 4 : f->vars.cap * 2;
        f->vars.vars = xrealloc(f->vars.vars, sizeof(tf_var) * f->vars.cap);
    }
    f->vars.vars[f->vars.len].name = name;
    f->vars.vars[f->vars.len].val = val;
    retain_obj(name);
    retain_obj(val);
    f->vars.len++;
}

static tf_obj *tf_var_fetch(tf_ctx *ctx, tf_obj *name) {
    for (int i = (int)ctx->cstack_len - 1; i >= 0; i--) {
        tf_frame *f = &ctx->call_stack[i];
        for (int j = (int)f->vars.len - 1; j >= 0; j--) {
            if (compare_string_obj(f->vars.vars[j].name, name) == 0) {
                return f->vars.vars[j].val;
            }
        }
    }
    return NULL;
}

static volatile sig_atomic_t interrupted = 0;
void handle_sigint(int sig) {
    (void)sig;
    interrupted = 1;
}

/*
 * The main iterative execution engine.
 * Instead of recursive C calls, it uses an explicit `call_stack` of frames.
 * This ensures deep user-defined word recursion does not overflow the C stack.
 */
int exec(tf_ctx *ctx, tf_obj *prg) {
    if (prg->type != TF_OBJ_TYPE_LIST) {
        tf_console_runtime_errorf("attempted to execute non-block object\n");
        return TF_ERR;
    }

    // push frame to the call stack
    cstack_push(ctx, prg);

    /* If this is a nested call to exec, we must continue to run until the
     * pushed frame is popped, to maintain the blocking semantics expected
     * by native words like 'if', 'while' etc. */
    size_t target_depth = ctx->cstack_len - 1;

    while (ctx->cstack_len > target_depth) {
        if (interrupted) {
            while (ctx->cstack_len > target_depth) { cstack_pop(ctx); }
            interrupted = 0;  // reset for next run
            return TF_INTERRUPTED;
        }

        tf_frame *f = &ctx->call_stack[ctx->cstack_len - 1];
        if (f->pc >= f->prg->list.len) {
            cstack_pop(ctx);
            continue;
        }

        tf_obj *o = f->prg->list.elem[f->pc++];
        switch (o->type) {
        case TF_OBJ_TYPE_SYMBOL:
            if (o->str.quoted) {
                fstack_push(ctx, o);
                retain_obj(o);
            } else {
                tf_func *func = get_func(ctx, o);
                if (!func) {
                    tf_console_runtime_errorf("undefined word '%s'\n",
                                              o->str.ptr);
                    while (ctx->cstack_len > target_depth) { cstack_pop(ctx); }
                    return TF_ERR;
                }
                int call_res = call_symbol(ctx, o);
                if (call_res == TF_INTERRUPTED) {
                    while (ctx->cstack_len > target_depth) { cstack_pop(ctx); }
                    return TF_INTERRUPTED;
                }
                if (call_res == TF_ERR) {
                    tf_console_runtime_errorf("execution of word '%s' failed\n",
                                              o->str.ptr);
                    // unwind remaining frames
                    while (ctx->cstack_len > target_depth) { cstack_pop(ctx); }
                    return TF_ERR;
                }
            }
            break;
        case TF_OBJ_TYPE_VARLIST:
            for (int i = (int)o->list.len - 1; i >= 0; i--) {
                tf_obj *val = fstack_pop(ctx);
                if (!val) {
                    tf_console_runtime_errorf(
                        "stack underflow during variable binding\n");
                    while (ctx->cstack_len > target_depth) { cstack_pop(ctx); }
                    return TF_ERR;
                }
                tf_var_bind(ctx, o->list.elem[i], val);
                release_obj(val);
            }
            break;
        case TF_OBJ_TYPE_VARFETCH: {
            tf_obj *val = tf_var_fetch(ctx, o);
            if (!val) {
                tf_console_runtime_errorf("undefined variable '$%s'\n",
                                          o->str.ptr);
                while (ctx->cstack_len > target_depth) { cstack_pop(ctx); }
                return TF_ERR;
            }
            fstack_push(ctx, val);
            retain_obj(val);
            break;
        }
        default:
            fstack_push(ctx, o);
            retain_obj(o);
            break;
        }
    }
    return TF_OK;
}

/*
 * Hybrid symbol dispatcher:
 * - User-defined words are pushed to the call_stack to continue iteration.
 * - Native words are called directly (recursive C call), which is safe
 *   as native words do not create deep call chains.
 */
int call_symbol(tf_ctx *ctx, tf_obj *symb) {
    tf_func *f = get_func(ctx, symb);
    if (!f) return TF_ERR;
    if (f->type == TF_FUNC_TYPE_USER) {
        cstack_push(ctx, f->user_impl);
        return TF_OK;
    } else {
        return f->native_impl(ctx);
    }
}

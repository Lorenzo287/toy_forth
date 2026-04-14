#ifndef TF_EXEC_H
#define TF_EXEC_H

#include "tf_obj.h"

typedef struct ctx tf_ctx;

typedef int (*tf_cb)(tf_ctx *ctx, char *name);

typedef enum { TF_FUNC_TYPE_NATIVE, TF_FUNC_TYPE_USER } tf_func_type;

// NOTE: use heap allocation (pointer indirection) for the name
// to be consistent with reference counting model
typedef struct {
    tf_obj *name;
    tf_func_type type;
    union {
        tf_cb native_impl;
        tf_obj *user_impl;
    };
} tf_func;

// NOTE: use double pointer indirection for the func list so that
// each function is allocated independently; when the func list grows we only
// possibly reallocate the pointers to func, never the functions themselves
struct ctx {
    tf_obj *stack;  // forth program stack
    tf_func **functions;
    size_t funcount;
};

size_t stack_len(tf_ctx *ctx);
tf_obj *stack_pop(tf_ctx *ctx);
tf_obj *stack_pop_type(tf_ctx *ctx, tf_type type);
void stack_push(tf_ctx *ctx, tf_obj *o);

tf_ctx *init_ctx(void);
tf_func *init_func(tf_ctx *ctx, tf_obj *name);
void set_native_func(tf_ctx *ctx, char *name, tf_cb cb);
void set_user_func(tf_ctx *ctx, tf_obj *name, tf_obj *uf);
tf_func *get_func(tf_ctx *ctx, tf_obj *name);

int exec(tf_ctx *ctx, tf_obj *prg);
int call_symbol(tf_ctx *ctx, tf_obj *symb);

#endif  // TF_EXEC_H

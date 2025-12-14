#ifndef TF_EXEC_H
#define TF_EXEC_H

#include "tf_obj.h"

typedef struct ctx tf_ctx;

typedef int (*tf_cb)(tf_ctx *ctx, char *name);

typedef struct {
    tf_obj *name;
    tf_cb callback;    // c func
    tf_obj *userfunc;  // user func
} tf_func;

struct ctx {
    tf_obj *stack;
    tf_func **functions;
    size_t funcount;
};

tf_ctx *init_ctx(void);
size_t stack_len(tf_ctx *ctx);
tf_obj *stack_pop(tf_ctx *ctx, tf_type type);
void stack_push(tf_ctx *ctx, tf_obj *o);

tf_func *init_func(tf_ctx *ctx, tf_obj *name);
void set_c_func(tf_ctx *ctx, char *name, tf_cb cb);
void set_user_func(tf_ctx *ctx, tf_obj *name, tf_obj *uf);

int exec(tf_ctx *ctx, tf_obj *prg);
int call_symbol(tf_ctx *ctx, tf_obj *symb);
tf_func *get_func(tf_ctx *ctx, tf_obj *name);

#endif  // TF_EXEC_H

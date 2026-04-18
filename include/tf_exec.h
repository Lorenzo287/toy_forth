#ifndef TF_EXEC_H
#define TF_EXEC_H

#include "tf_obj.h"

typedef struct ctx tf_ctx;

typedef int (*tf_cb)(tf_ctx *ctx);

typedef enum { TF_FUNC_TYPE_NATIVE, TF_FUNC_TYPE_USER } tf_func_type;

// NOTE: name is stored as a heap-allocated tf_obj* rather than a plain char*
// so that it participates in the reference counting model — retain_obj() is
// called when the function is registered and release_obj() when it is
// overwritten or freed, preventing leaks and keeping ownership rules uniform
// across all string data in the interpreter.
typedef struct {
    tf_obj *name;
    tf_func_type type;
    union {
        tf_cb native_impl;
        tf_obj *user_impl;
    };
} tf_func;

// NOTE: use double pointer indirection for the hash table buckets so that
// each tf_func is allocated independently at a stable heap address; when the
// table resizes and the buckets array is reallocated, any tf_func* pointer
// returned by get_func() remains valid. Storing tf_func structs directly in
// the buckets array would invalidate such pointers on resize.
typedef struct {
    tf_func **buckets;
    size_t capacity;
    size_t count;
} tf_func_table;

struct ctx {
    tf_obj *stack;  // forth program stack
    // tf_func_table functions;
    tf_func **functions;
    size_t funcount;
    tf_obj *curr_prg;  // current program list being executed
    size_t curr_pc;    // program counter (index into the curr_prg array)
};

size_t stack_len(tf_ctx *ctx);
tf_obj *stack_pop(tf_ctx *ctx);
tf_obj *stack_pop_type(tf_ctx *ctx, tf_type type);
void stack_push(tf_ctx *ctx, tf_obj *o);

tf_ctx *init_ctx(void);
void free_ctx(tf_ctx *ctx);
tf_func *init_func(tf_ctx *ctx, tf_obj *name);
void set_native_func(tf_ctx *ctx, char *name, tf_cb cb);
void set_user_func(tf_ctx *ctx, tf_obj *name, tf_obj *uf);
tf_func *get_func(tf_ctx *ctx, tf_obj *name);

int exec(tf_ctx *ctx, tf_obj *prg);
int call_symbol(tf_ctx *ctx, tf_obj *symb);

#endif  // TF_EXEC_H

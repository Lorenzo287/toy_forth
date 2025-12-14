#include "tf_lib.h"

int math_functions(tf_ctx *ctx, char *name) {
    if (stack_len(ctx) < 2) return TF_ERR;

    // WARN: it would be better to use a dedicated func to check the type,
    // instead of this weird type-dependent stack_pop and pop_obj

    tf_obj *b = stack_pop(ctx, TF_OBJ_TYPE_INT);
    if (b == NULL) return TF_ERR;
    tf_obj *a = stack_pop(ctx, TF_OBJ_TYPE_INT);
    if (a == NULL) {
        stack_push(ctx, b);
        return TF_ERR;
    }

    int result = 0;
    switch (name[0]) {
    case '+':
        result = a->i + b->i;
        break;
    case '-':
        result = a->i - b->i;
        break;
    case '*':
        result = a->i * b->i;
        break;
    case '/':
        result = a->i / b->i;
        break;
    }
    release_obj(a);
    release_obj(b);

    stack_push(ctx, create_int_obj(result));
    return TF_OK;
}

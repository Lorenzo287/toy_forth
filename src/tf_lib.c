#include "tf_lib.h"
#include <stdio.h>
#include <string.h>

int math_functions(tf_ctx *ctx, char *name) {
    if (stack_len(ctx) < 2) return TF_ERR;

    tf_obj *b = stack_pop_type(ctx, TF_OBJ_TYPE_INT);
    if (b == NULL) return TF_ERR;
    tf_obj *a = stack_pop_type(ctx, TF_OBJ_TYPE_INT);
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

int stack_functions(tf_ctx *ctx, char *name) {
    if (strcmp(name, "dup") == 0) {
        if (stack_len(ctx) < 1) return TF_ERR;
        tf_obj *o = ctx->stack->list.elem[ctx->stack->list.len - 1];
        stack_push(ctx, o);
        retain_obj(o);
    } else if (strcmp(name, "drop") == 0) {
        if (stack_len(ctx) < 1) return TF_ERR;
        tf_obj *o = stack_pop(ctx);
        release_obj(o);
    } else if (strcmp(name, "swap") == 0) {
        if (stack_len(ctx) < 2) return TF_ERR;
        tf_obj *a = stack_pop(ctx);
        tf_obj *b = stack_pop(ctx);
        stack_push(ctx, a);
        stack_push(ctx, b);
    } else if (strcmp(name, "over") == 0) {
        if (stack_len(ctx) < 2) return TF_ERR;
        tf_obj *o = ctx->stack->list.elem[ctx->stack->list.len - 2];
        stack_push(ctx, o);
        retain_obj(o);
    } else if (strcmp(name, "rot") == 0) {
        if (stack_len(ctx) < 3) return TF_ERR;
        tf_obj *c = stack_pop(ctx);
        tf_obj *b = stack_pop(ctx);
        tf_obj *a = stack_pop(ctx);
        stack_push(ctx, b);
        stack_push(ctx, c);
        stack_push(ctx, a);
    } else {
        return TF_ERR;
    }
    return TF_OK;
}

int io_functions(tf_ctx *ctx, char *name) {
    if (strcmp(name, "print") == 0) {
        if (stack_len(ctx) < 1) return TF_ERR;
        tf_obj *o = stack_pop(ctx);
        print_value(o);
        printf("\n");
        release_obj(o);
    } else {
        return TF_ERR;
    }
    return TF_OK;
}

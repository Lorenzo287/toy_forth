#include "tf_lib.h"
#include <stdio.h>
#include <string.h>

int math_functions(tf_ctx *ctx, char *name) {
    if (stack_len(ctx) < 2) return TF_ERR;

    tf_obj *b = stack_pop(ctx);
    tf_obj *a = stack_pop(ctx);

    if ((a->type != TF_OBJ_TYPE_INT && a->type != TF_OBJ_TYPE_FLOAT) ||
        (b->type != TF_OBJ_TYPE_INT && b->type != TF_OBJ_TYPE_FLOAT)) {
        stack_push(ctx, a);
        stack_push(ctx, b);
        return TF_ERR;
    }

    bool is_float =
        (a->type == TF_OBJ_TYPE_FLOAT || b->type == TF_OBJ_TYPE_FLOAT);
    char op = name[0];

    if (is_float) {  // at least one float
        float fa = (a->type == TF_OBJ_TYPE_FLOAT) ? a->f : (float)a->i;
        float fb = (b->type == TF_OBJ_TYPE_FLOAT) ? b->f : (float)b->i;
        float fresult = 0;

        if (op == '/' && fb == 0.0f) {
            stack_push(ctx, a);
            stack_push(ctx, b);
            return TF_ERR;
        }

        switch (op) {
        case '+':
            fresult = fa + fb;
            break;
        case '-':
            fresult = fa - fb;
            break;
        case '*':
            fresult = fa * fb;
            break;
        case '/':
            fresult = fa / fb;
            break;
        default:
            stack_push(ctx, a);
            stack_push(ctx, b);
            return TF_ERR;
        }
        stack_push(ctx, create_float_obj(fresult));
    } else {  // both int
        int ia = a->i;
        int ib = b->i;
        int iresult = 0;

        if ((op == '/' || op == '%') && ib == 0) {
            stack_push(ctx, a);
            stack_push(ctx, b);
            return TF_ERR;
        }

        switch (op) {
        case '+':
            iresult = ia + ib;
            break;
        case '-':
            iresult = ia - ib;
            break;
        case '*':
            iresult = ia * ib;
            break;
        case '/':
            iresult = ia / ib;
            break;
        case '%':
            iresult = ia % ib;
            break;
        default:
            return TF_ERR;
        }
        stack_push(ctx, create_int_obj(iresult));
    }

    release_obj(a);
    release_obj(b);
    return TF_OK;
}

int stack_functions(tf_ctx *ctx, char *name) {
    // duplicate top object
    if (strcmp(name, "dup") == 0) {
        if (stack_len(ctx) < 1) return TF_ERR;
        tf_obj *o = ctx->stack->list.elem[ctx->stack->list.len - 1];
        stack_push(ctx, o);
        retain_obj(o);
    }
    // pop top object
    else if (strcmp(name, "drop") == 0) {
        if (stack_len(ctx) < 1) return TF_ERR;
        tf_obj *o = stack_pop(ctx);
        release_obj(o);
    }
    // swap first two objects
    else if (strcmp(name, "swap") == 0) {
        if (stack_len(ctx) < 2) return TF_ERR;
        tf_obj *a = stack_pop(ctx);
        tf_obj *b = stack_pop(ctx);
        stack_push(ctx, a);
        stack_push(ctx, b);
    }
    // copy second obj on top
    else if (strcmp(name, "over") == 0) {
        if (stack_len(ctx) < 2) return TF_ERR;
        tf_obj *o = ctx->stack->list.elem[ctx->stack->list.len - 2];
        stack_push(ctx, o);
        retain_obj(o);
    }
    // move third obj on top
    else if (strcmp(name, "rot") == 0) {
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

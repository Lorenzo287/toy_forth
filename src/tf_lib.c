#include "tf_lib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <time.h>
#include "tf_obj.h"

/* Helper for binary math operations */
static int tf_binary_math(tf_ctx *ctx, char op) {
    if (fstack_len(ctx) < 2) return TF_ERR;

    tf_obj *b = fstack_pop(ctx);
    tf_obj *a = fstack_pop(ctx);

    if ((a->type != TF_OBJ_TYPE_INT && a->type != TF_OBJ_TYPE_FLOAT) ||
        (b->type != TF_OBJ_TYPE_INT && b->type != TF_OBJ_TYPE_FLOAT)) {
        fstack_push(ctx, a);
        fstack_push(ctx, b);
        return TF_ERR;
    }

    bool is_float =
        (a->type == TF_OBJ_TYPE_FLOAT || b->type == TF_OBJ_TYPE_FLOAT);

    if (is_float) {
        float fa = (a->type == TF_OBJ_TYPE_FLOAT) ? a->f : (float)a->i;
        float fb = (b->type == TF_OBJ_TYPE_FLOAT) ? b->f : (float)b->i;
        float fresult = 0;

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
            if (fb == 0.0f) {
                fstack_push(ctx, a);
                fstack_push(ctx, b);
                return TF_ERR;
            }
            fresult = fa / fb;
            break;
        case 'M':
            fresult = (fa > fb) ? fa : fb;
            break;  // max
        case 'm':
            fresult = (fa < fb) ? fa : fb;
            break;  // min
        default:
            fstack_push(ctx, a);
            fstack_push(ctx, b);
            return TF_ERR;
        }
        fstack_push(ctx, create_float_obj(fresult));
    } else {
        int ia = a->i;
        int ib = b->i;
        int iresult = 0;

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
        case '%':
            if (ib == 0) {
                fstack_push(ctx, a);
                fstack_push(ctx, b);
                return TF_ERR;
            }
            if (op == '/')
                iresult = ia / ib;
            else
                iresult = ia % ib;
            break;
        case 'M':
            iresult = (ia > ib) ? ia : ib;
            break;  // max
        case 'm':
            iresult = (ia < ib) ? ia : ib;
            break;  // min
        default:
            fstack_push(ctx, a);
            fstack_push(ctx, b);
            return TF_ERR;
        }
        fstack_push(ctx, create_int_obj(iresult));
    }

    release_obj(a);
    release_obj(b);
    return TF_OK;
}

int tf_add(tf_ctx *ctx) {
    return tf_binary_math(ctx, '+');
}
int tf_sub(tf_ctx *ctx) {
    return tf_binary_math(ctx, '-');
}
int tf_mul(tf_ctx *ctx) {
    return tf_binary_math(ctx, '*');
}
int tf_div(tf_ctx *ctx) {
    return tf_binary_math(ctx, '/');
}
int tf_mod(tf_ctx *ctx) {
    return tf_binary_math(ctx, '%');
}
int tf_max(tf_ctx *ctx) {
    return tf_binary_math(ctx, 'M');
}
int tf_min(tf_ctx *ctx) {
    return tf_binary_math(ctx, 'm');
}

int tf_neg(tf_ctx *ctx) {
    if (fstack_len(ctx) < 1) return TF_ERR;
    tf_obj *a = fstack_pop(ctx);
    if (a->type == TF_OBJ_TYPE_INT) {
        fstack_push(ctx, create_int_obj(-a->i));
    } else if (a->type == TF_OBJ_TYPE_FLOAT) {
        fstack_push(ctx, create_float_obj(-a->f));
    } else {
        fstack_push(ctx, a);
        return TF_ERR;
    }
    release_obj(a);
    return TF_OK;
}

int tf_abs(tf_ctx *ctx) {
    if (fstack_len(ctx) < 1) return TF_ERR;
    tf_obj *a = fstack_pop(ctx);
    if (a->type == TF_OBJ_TYPE_INT) {
        fstack_push(ctx, create_int_obj(a->i < 0 ? -a->i : a->i));
    } else if (a->type == TF_OBJ_TYPE_FLOAT) {
        fstack_push(ctx, create_float_obj(a->f < 0 ? -a->f : a->f));
    } else {
        fstack_push(ctx, a);
        return TF_ERR;
    }
    release_obj(a);
    return TF_OK;
}

int tf_dup(tf_ctx *ctx) {
    if (fstack_len(ctx) < 1) return TF_ERR;
    tf_obj *o = ctx->forth_stack->list.elem[ctx->forth_stack->list.len - 1];
    fstack_push(ctx, o);
    retain_obj(o);
    return TF_OK;
}

int tf_drop(tf_ctx *ctx) {
    if (fstack_len(ctx) < 1) return TF_ERR;
    tf_obj *o = fstack_pop(ctx);
    release_obj(o);
    return TF_OK;
}

int tf_swap(tf_ctx *ctx) {
    if (fstack_len(ctx) < 2) return TF_ERR;
    tf_obj *a = fstack_pop(ctx);
    tf_obj *b = fstack_pop(ctx);
    fstack_push(ctx, a);
    fstack_push(ctx, b);
    return TF_OK;
}

int tf_over(tf_ctx *ctx) {
    if (fstack_len(ctx) < 2) return TF_ERR;
    tf_obj *o = ctx->forth_stack->list.elem[ctx->forth_stack->list.len - 2];
    fstack_push(ctx, o);
    retain_obj(o);
    return TF_OK;
}

int tf_rot(tf_ctx *ctx) {
    if (fstack_len(ctx) < 3) return TF_ERR;
    tf_obj *c = fstack_pop(ctx);
    tf_obj *b = fstack_pop(ctx);
    tf_obj *a = fstack_pop(ctx);
    fstack_push(ctx, b);
    fstack_push(ctx, c);
    fstack_push(ctx, a);
    return TF_OK;
}

int tf_printf(tf_ctx *ctx) {
    if (fstack_len(ctx) < 1) return TF_ERR;
    tf_obj *o = fstack_pop(ctx);
    print_value(o);
    release_obj(o);
    return TF_OK;
}

int tf_print(tf_ctx *ctx) {
    if (fstack_len(ctx) < 1) return TF_ERR;
    tf_obj *o = fstack_pop(ctx);
    print_value(o);
    printf("\n");
    release_obj(o);
    return TF_OK;
}

int tf_dot(tf_ctx *ctx) {
    return tf_print(ctx);
}

int tf_stack(tf_ctx *ctx) {
    size_t len = fstack_len(ctx);
    printf("<%zu> ", len);
    for (size_t i = 0; i < len; i++) {
        print_value(ctx->forth_stack->list.elem[i]);
        printf(" ");
    }
    printf("\n");
    return TF_OK;
}

/* Helper for comparison operations */
static int tf_compare(tf_ctx *ctx, char *op) {
    if (fstack_len(ctx) < 2) return TF_ERR;

    tf_obj *b = fstack_pop(ctx);
    tf_obj *a = fstack_pop(ctx);

    bool result = false;

    if ((a->type == TF_OBJ_TYPE_INT || a->type == TF_OBJ_TYPE_FLOAT) &&
        (b->type == TF_OBJ_TYPE_INT || b->type == TF_OBJ_TYPE_FLOAT)) {
        float fa = (a->type == TF_OBJ_TYPE_FLOAT) ? a->f : (float)a->i;
        float fb = (b->type == TF_OBJ_TYPE_FLOAT) ? b->f : (float)b->i;

        if (!strcmp(op, "=="))
            result = (fa == fb);
        else if (!strcmp(op, "!="))
            result = (fa != fb);
        else if (!strcmp(op, "<"))
            result = (fa < fb);
        else if (!strcmp(op, ">"))
            result = (fa > fb);
        else if (!strcmp(op, "<="))
            result = (fa <= fb);
        else if (!strcmp(op, ">="))
            result = (fa >= fb);
    } else if (a->type == b->type) {
        if (!strcmp(op, "==")) {
            if (a->type == TF_OBJ_TYPE_BOOL)
                result = (a->b == b->b);
            else if (a->type == TF_OBJ_TYPE_STR ||
                     a->type == TF_OBJ_TYPE_SYMBOL)
                result = (compare_string_obj(a, b) == 0);
            else
                result = (a == b);
        } else if (!strcmp(op, "!=")) {
            if (a->type == TF_OBJ_TYPE_BOOL)
                result = (a->b != b->b);
            else if (a->type == TF_OBJ_TYPE_STR ||
                     a->type == TF_OBJ_TYPE_SYMBOL)
                result = (compare_string_obj(a, b) != 0);
            else
                result = (a != b);
        } else {
            fstack_push(ctx, a);
            fstack_push(ctx, b);
            return TF_ERR;
        }
    } else {
        if (!strcmp(op, "=="))
            result = false;
        else if (!strcmp(op, "!="))
            result = true;
        else {
            fstack_push(ctx, a);
            fstack_push(ctx, b);
            return TF_ERR;
        }
    }

    fstack_push(ctx, create_bool_obj(result));
    release_obj(a);
    release_obj(b);
    return TF_OK;
}

int tf_eq(tf_ctx *ctx) {
    return tf_compare(ctx, "==");
}
int tf_ne(tf_ctx *ctx) {
    return tf_compare(ctx, "!=");
}
int tf_lt(tf_ctx *ctx) {
    return tf_compare(ctx, "<");
}
int tf_gt(tf_ctx *ctx) {
    return tf_compare(ctx, ">");
}
int tf_le(tf_ctx *ctx) {
    return tf_compare(ctx, "<=");
}
int tf_ge(tf_ctx *ctx) {
    return tf_compare(ctx, ">=");
}

int tf_exec(tf_ctx *ctx) {
    if (fstack_len(ctx) < 1) return TF_ERR;
    tf_obj *o = fstack_pop(ctx);
    if (!o) return TF_ERR;

    if (o->type == TF_OBJ_TYPE_LIST) {
        cstack_push(ctx, o);
        release_obj(o);
        return TF_OK;
    } else if (o->type == TF_OBJ_TYPE_SYMBOL) {
        int res = call_symbol(ctx, o);
        release_obj(o);
        return res;
    }

    release_obj(o);
    return TF_ERR;
}

int tf_if_r(tf_ctx *ctx) {
    if (fstack_len(ctx) < 2) return TF_ERR;
    tf_obj *body = fstack_pop_type(ctx, TF_OBJ_TYPE_LIST);
    if (!body) return TF_ERR;

    tf_obj *cond = fstack_pop(ctx);
    if (!cond) {
        release_obj(body);
        return TF_ERR;
    }

    bool cond_val = false;
    if (cond->type == TF_OBJ_TYPE_BOOL) {
        cond_val = cond->b;
    } else if (cond->type == TF_OBJ_TYPE_LIST) {
        if (exec(ctx, cond) == TF_ERR) {
            release_obj(body);
            release_obj(cond);
            return TF_ERR;
        }
        tf_obj *res = fstack_pop_type(ctx, TF_OBJ_TYPE_BOOL);
        if (!res) {
            release_obj(body);
            release_obj(cond);
            return TF_ERR;
        }
        cond_val = res->b;
        release_obj(res);
    } else {
        release_obj(body);
        release_obj(cond);
        return TF_ERR;
    }

    int final_res = TF_OK;
    if (cond_val) { final_res = exec(ctx, body); }

    release_obj(body);
    release_obj(cond);
    return final_res;
}

int tf_ifelse_r(tf_ctx *ctx) {
    if (fstack_len(ctx) < 3) return TF_ERR;
    tf_obj *else_b = fstack_pop_type(ctx, TF_OBJ_TYPE_LIST);
    tf_obj *then_b = fstack_pop_type(ctx, TF_OBJ_TYPE_LIST);

    if (!else_b || !then_b) {
        if (else_b) release_obj(else_b);
        if (then_b) release_obj(then_b);
        return TF_ERR;
    }

    tf_obj *cond = fstack_pop(ctx);
    if (!cond) {
        release_obj(else_b);
        release_obj(then_b);
        return TF_ERR;
    }

    bool cond_val = false;
    if (cond->type == TF_OBJ_TYPE_BOOL) {
        cond_val = cond->b;
    } else if (cond->type == TF_OBJ_TYPE_LIST) {
        if (exec(ctx, cond) == TF_ERR) {
            release_obj(else_b);
            release_obj(then_b);
            release_obj(cond);
            return TF_ERR;
        }
        tf_obj *res = fstack_pop_type(ctx, TF_OBJ_TYPE_BOOL);
        if (!res) {
            release_obj(else_b);
            release_obj(then_b);
            release_obj(cond);
            return TF_ERR;
        }
        cond_val = res->b;
        release_obj(res);
    } else {
        release_obj(else_b);
        release_obj(then_b);
        release_obj(cond);
        return TF_ERR;
    }

    int final_res = TF_OK;
    if (cond_val) {
        final_res = exec(ctx, then_b);
    } else {
        final_res = exec(ctx, else_b);
    }

    release_obj(else_b);
    release_obj(then_b);
    release_obj(cond);
    return final_res;
}

int tf_times_r(tf_ctx *ctx) {
    if (fstack_len(ctx) < 2) return TF_ERR;
    tf_obj *body = fstack_pop_type(ctx, TF_OBJ_TYPE_LIST);
    tf_obj *n_obj = fstack_pop_type(ctx, TF_OBJ_TYPE_INT);
    if (!body || !n_obj) {
        if (body) release_obj(body);
        if (n_obj) release_obj(n_obj);
        return TF_ERR;
    }

    int n = n_obj->i;
    int res = TF_OK;
    for (int i = 0; i < n; i++) {
        res = exec(ctx, body);
        if (res == TF_ERR) break;
    }

    release_obj(body);
    release_obj(n_obj);
    return res;
}

int tf_each_r(tf_ctx *ctx) {
    if (fstack_len(ctx) < 2) return TF_ERR;
    tf_obj *body = fstack_pop_type(ctx, TF_OBJ_TYPE_LIST);
    tf_obj *data = fstack_pop_type(ctx, TF_OBJ_TYPE_LIST);
    if (!body || !data) {
        if (body) release_obj(body);
        if (data) release_obj(data);
        return TF_ERR;
    }

    int res = TF_OK;
    for (size_t i = 0; i < data->list.len; i++) {
        fstack_push(ctx, data->list.elem[i]);
        retain_obj(data->list.elem[i]);
        res = exec(ctx, body);
        if (res == TF_ERR) break;
    }

    release_obj(body);
    release_obj(data);
    return res;
}

int tf_while_r(tf_ctx *ctx) {
    if (fstack_len(ctx) < 2) return TF_ERR;
    tf_obj *body = fstack_pop_type(ctx, TF_OBJ_TYPE_LIST);
    tf_obj *cond = fstack_pop_type(ctx, TF_OBJ_TYPE_LIST);

    if (!body || !cond) {
        if (body) release_obj(body);
        if (cond) release_obj(cond);
        return TF_ERR;
    }

    int final_res = TF_OK;
    while (1) {
        if (exec(ctx, cond) == TF_ERR) {
            final_res = TF_ERR;
            break;
        }

        tf_obj *res = fstack_pop_type(ctx, TF_OBJ_TYPE_BOOL);
        if (!res) {
            final_res = TF_ERR;
            break;
        }

        bool continue_loop = res->b;
        release_obj(res);

        if (!continue_loop) break;

        if (exec(ctx, body) == TF_ERR) {
            final_res = TF_ERR;
            break;
        }
    }

    release_obj(body);
    release_obj(cond);
    return final_res;
}

int tf_colon(tf_ctx *ctx) {
    if (ctx->cstack_len == 0) return TF_ERR;
    tf_frame *f = &ctx->call_stack[ctx->cstack_len - 1];

    if (f->pc >= f->prg->list.len) return TF_ERR;
    tf_obj *func_name = f->prg->list.elem[f->pc];
    if (func_name->type != TF_OBJ_TYPE_SYMBOL) return TF_ERR;

    tf_obj *body = init_list_obj();
    f->pc++;
    while (f->pc < f->prg->list.len) {
        tf_obj *o = f->prg->list.elem[f->pc];
        if (o->type == TF_OBJ_TYPE_SYMBOL && strcmp(o->str.ptr, ";") == 0) {
            break;
        }
        push_obj(body, o);
        retain_obj(o);
        f->pc++;
    }

    if (f->pc >= f->prg->list.len) {
        release_obj(body);
        return TF_ERR;
    }

    set_user_func(ctx, func_name, body);
    release_obj(body);
    f->pc++;
    return TF_OK;
}

int tf_def(tf_ctx *ctx) {
    if (fstack_len(ctx) < 2) return TF_ERR;

    tf_obj *body = fstack_pop_type(ctx, TF_OBJ_TYPE_LIST);
    tf_obj *func_name = fstack_pop_type(ctx, TF_OBJ_TYPE_SYMBOL);

    if (!body || !func_name) {
        if (body) release_obj(body);
        if (func_name) release_obj(func_name);
        return TF_ERR;
    }

    set_user_func(ctx, func_name, body);

    release_obj(body);
    release_obj(func_name);
    return TF_OK;
}

int tf_geth(tf_ctx *ctx) {
    tf_obj *idx_obj = fstack_pop_type(ctx, TF_OBJ_TYPE_INT);
    tf_obj *list_obj = fstack_pop_type(ctx, TF_OBJ_TYPE_LIST);
    if (!idx_obj || !list_obj) return TF_ERR;

    int idx = idx_obj->i;
    if (idx < 0 || idx >= (int)list_obj->list.len) {
        release_obj(idx_obj);
        release_obj(list_obj);
        return TF_ERR;
    }

    tf_obj *result = list_obj->list.elem[idx];
    retain_obj(result);
    fstack_push(ctx, result);

    release_obj(idx_obj);
    release_obj(list_obj);
    return TF_OK;
}

int tf_seth(tf_ctx *ctx) {
    if (fstack_len(ctx) < 3) return TF_ERR;

    tf_obj *val = fstack_pop(ctx);
    tf_obj *idx_obj = fstack_pop_type(ctx, TF_OBJ_TYPE_INT);
    if (!idx_obj) {
        release_obj(val);
        return TF_ERR;
    }

    tf_obj *list_obj = fstack_pop_type(ctx, TF_OBJ_TYPE_LIST);
    if (!list_obj) {
        release_obj(val);
        release_obj(idx_obj);
        return TF_ERR;
    }

    int idx = idx_obj->i;
    if (idx < 0 || idx >= (int)list_obj->list.len) {
        release_obj(val);
        release_obj(idx_obj);
        release_obj(list_obj);
        return TF_ERR;
    }

    release_obj(list_obj->list.elem[idx]);
    list_obj->list.elem[idx] = val;
    release_obj(idx_obj);
    release_obj(list_obj);
    return TF_OK;
}

int tf_len(tf_ctx *ctx) {
    if (fstack_len(ctx) < 1) return TF_ERR;
    tf_obj *o = fstack_pop(ctx);
    if (o->type != TF_OBJ_TYPE_LIST) {
        release_obj(o);
        return TF_ERR;
    }
    fstack_push(ctx, create_int_obj((int)o->list.len));
    release_obj(o);
    return TF_OK;
}

int tf_rand(tf_ctx *ctx) {
    fstack_push(ctx, create_int_obj(rand()));
    return TF_OK;
}

int tf_sleep(tf_ctx *ctx) {
    tf_obj *ms_obj = fstack_pop_type(ctx, TF_OBJ_TYPE_INT);
    if (!ms_obj) return TF_ERR;
#ifdef _WIN32
    Sleep(ms_obj->i);
#else
    usleep(ms_obj->i * 1000);
#endif
    release_obj(ms_obj);
    return TF_OK;
}

int tf_key(tf_ctx *ctx) {
    int c = getchar();
    if (c == EOF) return TF_ERR;
    fstack_push(ctx, create_int_obj(c));
    return TF_OK;
}

int tf_time(tf_ctx *ctx) {
    fstack_push(ctx, create_int_obj((int)clock()));
    return TF_OK;
}

int tf_exit(tf_ctx *ctx) {
    (void)ctx;
    exit(0);
    return TF_OK;
}

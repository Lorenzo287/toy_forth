#include "tf_lib.h"
#include <stdio.h>
#include <string.h>
#include "tf_obj.h"

int math_functions(tf_ctx *ctx, char *name) {
    // Unary operations
    if (strcmp(name, "abs") == 0) {
        if (stack_len(ctx) < 1) return TF_ERR;
        tf_obj *a = stack_pop(ctx);
        if (a->type == TF_OBJ_TYPE_INT) {
            stack_push(ctx, create_int_obj(a->i < 0 ? -a->i : a->i));
        } else if (a->type == TF_OBJ_TYPE_FLOAT) {
            stack_push(ctx, create_float_obj(a->f < 0 ? -a->f : a->f));
        } else {
            stack_push(ctx, a);
            return TF_ERR;
        }
        release_obj(a);
        return TF_OK;
    }

    // Binary operations
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

    if (is_float) {  // at least one float
        float fa = (a->type == TF_OBJ_TYPE_FLOAT) ? a->f : (float)a->i;
        float fb = (b->type == TF_OBJ_TYPE_FLOAT) ? b->f : (float)b->i;
        float fresult = 0;

        if (strcmp(name, "+") == 0)
            fresult = fa + fb;
        else if (strcmp(name, "-") == 0)
            fresult = fa - fb;
        else if (strcmp(name, "*") == 0)
            fresult = fa * fb;
        else if (strcmp(name, "/") == 0) {
            if (fb == 0.0f) {
                stack_push(ctx, a);
                stack_push(ctx, b);
                return TF_ERR;
            }
            fresult = fa / fb;
        } else if (strcmp(name, "max") == 0)
            fresult = (fa > fb) ? fa : fb;
        else if (strcmp(name, "min") == 0)
            fresult = (fa < fb) ? fa : fb;
        else {
            stack_push(ctx, a);
            stack_push(ctx, b);
            return TF_ERR;
        }
        stack_push(ctx, create_float_obj(fresult));
    } else {  // both int
        int ia = a->i;
        int ib = b->i;
        int iresult = 0;

        if (strcmp(name, "+") == 0)
            iresult = ia + ib;
        else if (strcmp(name, "-") == 0)
            iresult = ia - ib;
        else if (strcmp(name, "*") == 0)
            iresult = ia * ib;
        else if (strcmp(name, "/") == 0 || strcmp(name, "%") == 0 ||
                 strcmp(name, "mod") == 0) {
            if (ib == 0) {
                stack_push(ctx, a);
                stack_push(ctx, b);
                return TF_ERR;
            }
            if (strcmp(name, "/") == 0)
                iresult = ia / ib;
            else
                iresult = ia % ib;
        } else if (strcmp(name, "max") == 0)
            iresult = (ia > ib) ? ia : ib;
        else if (strcmp(name, "min") == 0)
            iresult = (ia < ib) ? ia : ib;
        else {
            stack_push(ctx, a);
            stack_push(ctx, b);
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
    if (strcmp(name, "print") == 0 || strcmp(name, ".") == 0 ||
        strcmp(name, "println") == 0) 
    {
        if (stack_len(ctx) < 1) return TF_ERR;
        tf_obj *o = stack_pop(ctx);
        print_value(o);
        if (strcmp(name, "println") == 0) printf("\n");
        release_obj(o);
    } else if (strcmp(name, ".s") == 0) {
        // could use print_value(ctx->stack) but that would print '[ ... ]',
        // this way we omit the brackets
        size_t len = stack_len(ctx);
        printf("<%zu> ", len);
        for (size_t i = 0; i < len; i++) {
            print_value(ctx->stack->list.elem[i]);
            printf(" ");
        }
        printf("\n");
    } else {
        return TF_ERR;
    }
    return TF_OK;
}

int compare_functions(tf_ctx *ctx, char *name) {
    if (stack_len(ctx) < 2) return TF_ERR;

    tf_obj *b = stack_pop(ctx);
    tf_obj *a = stack_pop(ctx);

    bool result = false;

    // Numeric comparison
    if ((a->type == TF_OBJ_TYPE_INT || a->type == TF_OBJ_TYPE_FLOAT) &&
        (b->type == TF_OBJ_TYPE_INT || b->type == TF_OBJ_TYPE_FLOAT)) {
        float fa = (a->type == TF_OBJ_TYPE_FLOAT) ? a->f : (float)a->i;
        float fb = (b->type == TF_OBJ_TYPE_FLOAT) ? b->f : (float)b->i;

        if (!strcmp(name, "=="))
            result = (fa == fb);
        else if (!strcmp(name, "!="))
            result = (fa != fb);
        else if (!strcmp(name, "<"))
            result = (fa < fb);
        else if (!strcmp(name, ">"))
            result = (fa > fb);
        else if (!strcmp(name, "<="))
            result = (fa <= fb);
        else if (!strcmp(name, ">="))
            result = (fa >= fb);
    }
    // Boolean or other type equality
    else if (a->type == b->type) {
        if (!strcmp(name, "==")) {
            if (a->type == TF_OBJ_TYPE_BOOL)
                result = (a->b == b->b);
            else if (a->type == TF_OBJ_TYPE_STR ||
                     a->type == TF_OBJ_TYPE_SYMBOL)
                result = (compare_string_obj(a, b) == 0);
            else
                result = (a == b);  // pointer comparison for other types
        } else if (!strcmp(name, "!=")) {
            if (a->type == TF_OBJ_TYPE_BOOL)
                result = (a->b != b->b);
            else if (a->type == TF_OBJ_TYPE_STR ||
                     a->type == TF_OBJ_TYPE_SYMBOL)
                result = (compare_string_obj(a, b) != 0);
            else
                result = (a != b);
        } else {
            stack_push(ctx, a);
            stack_push(ctx, b);
            return TF_ERR;
        }
    } else {
        if (!strcmp(name, "=="))
            result = false;
        else if (!strcmp(name, "!="))
            result = true;
        else {
            stack_push(ctx, a);
            stack_push(ctx, b);
            return TF_ERR;
        }
    }

    stack_push(ctx, create_bool_obj(result));
    release_obj(a);
    release_obj(b);
    return TF_OK;
}

int control_functions(tf_ctx *ctx, char *name) {
    if (strcmp(name, "exec") == 0) {
        if (stack_len(ctx) < 1) return TF_ERR;
        tf_obj *code = stack_pop_type(ctx, TF_OBJ_TYPE_LIST);
        if (!code) return TF_ERR;
        int res = exec(ctx, code);
        release_obj(code);
        return res;
    } else if (strcmp(name, "if") == 0) {
        if (stack_len(ctx) < 2) return TF_ERR;
        tf_obj *body = stack_pop_type(ctx, TF_OBJ_TYPE_LIST);
        tf_obj *cond = stack_pop(ctx);
        if (!body || !cond) {
            if (body) release_obj(body);
            if (cond) release_obj(cond);
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
            tf_obj *res = stack_pop_type(ctx, TF_OBJ_TYPE_BOOL);
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
    } else if (strcmp(name, "ifelse") == 0) {
        if (stack_len(ctx) < 3) return TF_ERR;
        tf_obj *else_b = stack_pop_type(ctx, TF_OBJ_TYPE_LIST);
        tf_obj *then_b = stack_pop_type(ctx, TF_OBJ_TYPE_LIST);
        tf_obj *cond = stack_pop(ctx);

        if (!else_b || !then_b || !cond) {
            if (else_b) release_obj(else_b);
            if (then_b) release_obj(then_b);
            if (cond) release_obj(cond);
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
            tf_obj *res = stack_pop_type(ctx, TF_OBJ_TYPE_BOOL);
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
    } else if (strcmp(name, "times") == 0) {
        if (stack_len(ctx) < 2) return TF_ERR;
        tf_obj *body = stack_pop_type(ctx, TF_OBJ_TYPE_LIST);
        tf_obj *n_obj = stack_pop_type(ctx, TF_OBJ_TYPE_INT);
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
    } else if (strcmp(name, "each") == 0) {
        if (stack_len(ctx) < 2) return TF_ERR;
        tf_obj *body = stack_pop_type(ctx, TF_OBJ_TYPE_LIST);
        tf_obj *data = stack_pop_type(ctx, TF_OBJ_TYPE_LIST);
        if (!body || !data) {
            if (body) release_obj(body);
            if (data) release_obj(data);
            return TF_ERR;
        }

        int res = TF_OK;
        for (size_t i = 0; i < data->list.len; i++) {
            stack_push(ctx, data->list.elem[i]);
            retain_obj(data->list.elem[i]);
            res = exec(ctx, body);
            if (res == TF_ERR) break;
        }

        release_obj(body);
        release_obj(data);
        return res;
    } else if (strcmp(name, "while") == 0) {
        if (stack_len(ctx) < 2) return TF_ERR;
        tf_obj *body = stack_pop_type(ctx, TF_OBJ_TYPE_LIST);
        tf_obj *cond = stack_pop_type(ctx, TF_OBJ_TYPE_LIST);

        if (!body || !cond) {
            if (body) release_obj(body);
            if (cond) release_obj(cond);
            return TF_ERR;
        }

        int final_res = TF_OK;
        while (1) {
            // 1. Exec condition
            if (exec(ctx, cond) == TF_ERR) {
                final_res = TF_ERR;
                break;
            }

            // 2. Check result
            tf_obj *res = stack_pop_type(ctx, TF_OBJ_TYPE_BOOL);
            if (!res) {
                final_res = TF_ERR;
                break;
            }

            bool continue_loop = res->b;
            release_obj(res);

            if (!continue_loop) break;

            // 3. Exec body
            if (exec(ctx, body) == TF_ERR) {
                final_res = TF_ERR;
                break;
            }
        }

        release_obj(body);
        release_obj(cond);
        return final_res;
    }
    return TF_ERR;
}

int definition_functions(tf_ctx *ctx, char *name) {
    if (strcmp(name, ":") == 0) {
        // 1. Get name (next token)
        ctx->curr_pc++;
        if (ctx->curr_pc >= ctx->curr_prg->list.len) return TF_ERR;
        tf_obj *func_name = ctx->curr_prg->list.elem[ctx->curr_pc];
        if (func_name->type != TF_OBJ_TYPE_SYMBOL) return TF_ERR;

        // 2. Collect body until ";"
        tf_obj *body = init_list_obj();
        ctx->curr_pc++;
        while (ctx->curr_pc < ctx->curr_prg->list.len) {
            tf_obj *o = ctx->curr_prg->list.elem[ctx->curr_pc];
            if (o->type == TF_OBJ_TYPE_SYMBOL && strcmp(o->str.ptr, ";") == 0) {
                break;
            }
            push_obj(body, o);
            retain_obj(o);
            ctx->curr_pc++;
        }

        if (ctx->curr_pc >= ctx->curr_prg->list.len) {
            release_obj(body);
            return TF_ERR;  // ";" not found
        }

        // 3. Register function
        set_user_func(ctx, func_name, body);
        release_obj(body);
        return TF_OK;
    } else if (strcmp(name, "def") == 0) {
        if (stack_len(ctx) < 2) return TF_ERR;

        tf_obj *body = stack_pop_type(ctx, TF_OBJ_TYPE_LIST);
        tf_obj *func_name = stack_pop_type(ctx, TF_OBJ_TYPE_SYMBOL);

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
    return TF_ERR;
}

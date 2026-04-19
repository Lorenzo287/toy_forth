#include "tf_obj.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tf_alloc.h"

/* === Object Creation === */

tf_obj *init_obj(int type) {
    tf_obj *o = xmalloc(sizeof(tf_obj));
    o->type = type;
    o->refcount = 1;
    return o;
}

tf_obj *init_list_obj(void) {
    tf_obj *o = init_obj(TF_OBJ_TYPE_LIST);
    o->list.elem = NULL;
    o->list.len = 0;
    o->list.cap = 1;
    o->list.elem = xmalloc(sizeof(tf_obj *) * o->list.cap);
    return o;
}

tf_obj *create_int_obj(int i) {
    tf_obj *o = init_obj(TF_OBJ_TYPE_INT);
    o->i = i;
    return o;
}

tf_obj *create_bool_obj(bool b) {
    tf_obj *o = init_obj(TF_OBJ_TYPE_BOOL);
    o->b = b;
    return o;
}

tf_obj *create_float_obj(float f) {
    tf_obj *o = init_obj(TF_OBJ_TYPE_FLOAT);
    o->f = f;
    return o;
}

tf_obj *create_symbol_obj(char *s, size_t len) {
    tf_obj *o = create_string_obj(s, len);
    o->type = TF_OBJ_TYPE_SYMBOL;
    return o;
}

tf_obj *create_string_obj(char *s, size_t len) {
    tf_obj *o = init_obj(TF_OBJ_TYPE_STR);
    o->str.ptr = xmalloc(len + 1);
    o->str.len = len;
    o->str.quoted = false;
    memcpy(o->str.ptr, s, len);
    o->str.ptr[len] = 0;
    return o;
}

/* === Object Utilities === */

int compare_string_obj(tf_obj *a, tf_obj *b) {
    size_t min_len = a->str.len < b->str.len ? a->str.len : b->str.len;
    int cmp = memcmp(a->str.ptr, b->str.ptr, min_len);
    if (cmp == 0) {
        if (a->str.len == b->str.len)
            return 0;
        else if (a->str.len > b->str.len)
            return 1;
        else
            return -1;
    } else {
        if (cmp < 0) return -1;
        return 1;
    }
    return 0;
}

void push_obj(tf_obj *l, tf_obj *elem) {
    if (l->list.len >= l->list.cap) {
        l->list.cap *= 2;
        l->list.elem = xrealloc(l->list.elem, sizeof(tf_obj *) * l->list.cap);
    }
    l->list.elem[l->list.len++] = elem;
}

// pop object only if the type is correct
tf_obj *pop_obj_type(tf_obj *l, tf_type type) {
    if (l->list.len == 0) return NULL;
    tf_obj *o = l->list.elem[l->list.len - 1];
    if (o->type != type) return NULL;
    return pop_obj(l);
}

tf_obj *pop_obj(tf_obj *l) {
    if (l->list.len == 0) return NULL;
    tf_obj *o = l->list.elem[l->list.len - 1];

    l->list.len--;
    if (l->list.len < l->list.cap / 2 && l->list.cap > 1) {
        l->list.cap /= 2;
        l->list.elem = xrealloc(l->list.elem, sizeof(tf_obj *) * l->list.cap);
    }
    return o;
}

void retain_obj(tf_obj *o) {
    o->refcount++;
}

void release_obj(tf_obj *o) {
    assert(o->refcount > 0);
    o->refcount--;
    if (o->refcount == 0) free_obj(o);
}

void free_obj(tf_obj *o) {
    switch (o->type) {
    case TF_OBJ_TYPE_VARLIST:
    case TF_OBJ_TYPE_LIST:
        for (size_t i = 0; i < o->list.len; i++) release_obj(o->list.elem[i]);
        free(o->list.elem);
        break;
    case TF_OBJ_TYPE_VARFETCH:
    case TF_OBJ_TYPE_SYMBOL:
    case TF_OBJ_TYPE_STR:
        free(o->str.ptr);
        break;
    default:
        break;
    }
    free(o);
}

// Print the object with type information (for debugging)
void print_obj(tf_obj *o, size_t *count) {
    (*count)++;
    switch (o->type) {
    case TF_OBJ_TYPE_INT:
        printf("{int:%d}", o->i);
        break;
    case TF_OBJ_TYPE_FLOAT:
        printf("{float:%g}", o->f);
        break;
    case TF_OBJ_TYPE_SYMBOL:
        printf("{symbol:%s%s}", o->str.quoted ? "'" : "", o->str.ptr);
        break;
    case TF_OBJ_TYPE_STR:
        printf("{string:\"%s\"}", o->str.ptr);
        break;
    case TF_OBJ_TYPE_BOOL:
        printf("{bool:%d}", o->b);
        break;
    case TF_OBJ_TYPE_VARFETCH:
        printf("{fetch:$%s}", o->str.ptr);
        break;
    case TF_OBJ_TYPE_VARLIST:
        (*count)--;
        printf("(");
        for (size_t i = 0; i < o->list.len; i++) {
            print_obj(o->list.elem[i], count);
            if (i != o->list.len - 1) printf(" ");
        }
        printf(")");
        break;
    case TF_OBJ_TYPE_LIST:
        (*count)--;
        printf("[");
        for (size_t i = 0; i < o->list.len; i++) {
            print_obj(o->list.elem[i], count);
            if (i != o->list.len - 1) {
                if (*count % 6 == 0)
                    printf("\n");
                else
                    printf(" ");
            }
        }
        printf("]");
        break;
    default:
        printf("?");
    }
}

// Print the raw value of the object (for Forth 'print' word)
void print_value(tf_obj *o) {
    switch (o->type) {
    case TF_OBJ_TYPE_INT:
        printf("%d", o->i);
        break;
    case TF_OBJ_TYPE_FLOAT:
        printf("%g", o->f);
        break;
    case TF_OBJ_TYPE_SYMBOL:
        printf("%s", o->str.ptr);
        break;
    case TF_OBJ_TYPE_STR:
        printf("%s", o->str.ptr);
        break;
    case TF_OBJ_TYPE_BOOL:
        printf("%s", o->b ? "true" : "false");
        break;
    case TF_OBJ_TYPE_VARFETCH:
        printf("$%s", o->str.ptr);
        break;
    case TF_OBJ_TYPE_VARLIST:
        printf("(");
        for (size_t i = 0; i < o->list.len; i++) {
            print_value(o->list.elem[i]);
            if (i != o->list.len - 1) printf(" ");
        }
        printf(")");
        break;
    case TF_OBJ_TYPE_LIST:
        printf("[");
        for (size_t i = 0; i < o->list.len; i++) {
            print_value(o->list.elem[i]);
            if (i != o->list.len - 1) printf(" ");
        }
        printf("]");
        break;
    default:
        printf("?");
    }
}

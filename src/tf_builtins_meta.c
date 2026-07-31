#include "tf_builtins.h"
#include <inttypes.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tf_alloc.h"
#include "tf_docs.h"
#include "tf_exec.h"
#include "tf_obj.h"

tf_ret tf_number_q(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *o = tf_stack_pop(ctx);
    tf_type type = tf_obj_typeof(o);
    tf_stack_push(ctx, tf_obj_new_bool(type == TF_OBJ_TYPE_INT ||
                                      type == TF_OBJ_TYPE_FLOAT));
    tf_obj_release(o);
    return TF_OK;
}

static bool parse_int_text(tf_obj *text, int64_t *result) {
    if (text->str.len == 0 ||
        memchr(text->str.ptr, '\0', text->str.len) != NULL ||
        isspace((unsigned char)text->str.ptr[0])) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    long long value = strtoll(text->str.ptr, &end, 10);
    if (errno == ERANGE || end != text->str.ptr + text->str.len) return false;
    if (value < INT64_MIN || value > INT64_MAX) return false;
    *result = (int64_t)value;
    return true;
}

static bool parse_float_text(tf_obj *text, double *result) {
    if (text->str.len == 0 ||
        memchr(text->str.ptr, '\0', text->str.len) != NULL ||
        isspace((unsigned char)text->str.ptr[0])) {
        return false;
    }
    size_t position = 0;
    if (text->str.ptr[position] == '+' || text->str.ptr[position] == '-') {
        position++;
    }
    size_t integer_start = position;
    while (position < text->str.len &&
           isdigit((unsigned char)text->str.ptr[position])) {
        position++;
    }
    if (position == integer_start) return false;
    if (position < text->str.len && text->str.ptr[position] == '.') {
        position++;
        while (position < text->str.len &&
               isdigit((unsigned char)text->str.ptr[position])) {
            position++;
        }
    }
    if (position < text->str.len &&
        (text->str.ptr[position] == 'e' ||
         text->str.ptr[position] == 'E')) {
        position++;
        if (position < text->str.len &&
            (text->str.ptr[position] == '+' ||
             text->str.ptr[position] == '-')) {
            position++;
        }
        size_t exponent_start = position;
        while (position < text->str.len &&
               isdigit((unsigned char)text->str.ptr[position])) {
            position++;
        }
        if (position == exponent_start) return false;
    }
    if (position != text->str.len) return false;

    errno = 0;
    char *end = NULL;
    double value = strtod(text->str.ptr, &end);
    if (errno == ERANGE || end != text->str.ptr + text->str.len ||
        !isfinite(value)) {
        return false;
    }
    *result = value;
    return true;
}

tf_ret tf_to_int(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *value = tf_stack_peek(ctx, 0);
    tf_type type = tf_obj_typeof(value);
    if (type == TF_OBJ_TYPE_INT) return TF_OK;

    int64_t result = 0;
    if (type == TF_OBJ_TYPE_FLOAT) {
        const double upper_bound = 9223372036854775808.0;
        if (!isfinite(value->f) || trunc(value->f) != value->f ||
            value->f < (double)INT64_MIN || value->f >= upper_bound) {
            tf_ctx_runtime_errorf(
                ctx, "'>int' expected an integral float in the int range\n");
            return TF_ERR;
        }
        result = (int64_t)value->f;
    } else if (type == TF_OBJ_TYPE_STR) {
        if (!parse_int_text(value, &result)) {
            tf_ctx_runtime_errorf(
                ctx, "'>int' expected strict decimal integer text\n");
            return TF_ERR;
        }
    } else {
        tf_ctx_runtime_errorf(
            ctx, "'>int' expected int, float, or string, found %s\n",
            tf_obj_type_name(value));
        return TF_ERR;
    }

    value = tf_stack_pop(ctx);
    tf_stack_push(ctx, tf_obj_new_int(result));
    tf_obj_release(value);
    return TF_OK;
}

tf_ret tf_to_float(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *value = tf_stack_peek(ctx, 0);
    tf_type type = tf_obj_typeof(value);
    if (type == TF_OBJ_TYPE_FLOAT) return TF_OK;

    double result = 0.0;
    if (type == TF_OBJ_TYPE_INT) {
        result = (double)tf_obj_int_value(value);
    } else if (type == TF_OBJ_TYPE_STR) {
        if (!parse_float_text(value, &result)) {
            tf_ctx_runtime_errorf(
                ctx, "'>float' expected strict finite decimal numeric text\n");
            return TF_ERR;
        }
    } else {
        tf_ctx_runtime_errorf(
            ctx, "'>float' expected int, float, or string, found %s\n",
            tf_obj_type_name(value));
        return TF_ERR;
    }

    value = tf_stack_pop(ctx);
    tf_stack_push(ctx, tf_obj_new_float(result));
    tf_obj_release(value);
    return TF_OK;
}

tf_ret tf_sequence_q(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *o = tf_stack_pop(ctx);
    tf_type type = tf_obj_typeof(o);
    tf_stack_push(ctx, tf_obj_new_bool(type == TF_OBJ_TYPE_VECTOR ||
                                      type == TF_OBJ_TYPE_LIST ||
                                      type == TF_OBJ_TYPE_STR));
    tf_obj_release(o);
    return TF_OK;
}

tf_ret tf_callable_q(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *o = tf_stack_pop(ctx);
    tf_type type = tf_obj_typeof(o);
    bool result =
        type == TF_OBJ_TYPE_VECTOR ||
        ((type == TF_OBJ_TYPE_SYMBOL || type == TF_OBJ_TYPE_CALL) &&
         tf_dict_lookup(ctx, o) != NULL);
    tf_stack_push(ctx, tf_obj_new_bool(result));
    tf_obj_release(o);
    return TF_OK;
}

static tf_ret package_declaration_only(tf_ctx *ctx) {
    tf_ctx_runtime_errorf(
        ctx, "'%s' is only valid as a top-level package declaration\n",
        ctx->current_word);
    return TF_ERR;
}

tf_ret tf_package_declaration(tf_ctx *ctx) {
    return package_declaration_only(ctx);
}

tf_ret tf_import_declaration(tf_ctx *ctx) {
    if (tf_current_package_index(ctx) != TF_ROOT_PACKAGE) {
        return package_declaration_only(ctx);
    }
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_STR)) return TF_ERR;
    tf_obj *path = tf_stack_pop_type(ctx, TF_OBJ_TYPE_STR);
    tf_ret status = TF_ERR;
    if (path->str.len != strlen(path->str.ptr)) {
        tf_ctx_runtime_errorf(ctx, "package import paths cannot contain NUL bytes\n");
    } else {
        status = tf_package_load(ctx, path->str.ptr, TF_ROOT_PACKAGE,
                                 NULL, 0, NULL);
    }
    tf_obj_release(path);
    return status;
}

tf_ret tf_import_as_declaration(tf_ctx *ctx) {
    if (tf_current_package_index(ctx) != TF_ROOT_PACKAGE) {
        return package_declaration_only(ctx);
    }
    if (!tf_ctx_require_stack(ctx, 2) ||
        !tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_SYMBOL) ||
        !tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_STR)) {
        return TF_ERR;
    }
    tf_obj *alias = tf_stack_pop_type(ctx, TF_OBJ_TYPE_SYMBOL);
    tf_obj *path = tf_stack_pop_type(ctx, TF_OBJ_TYPE_STR);
    tf_ret status = TF_ERR;
    if (path->str.len != strlen(path->str.ptr)) {
        tf_ctx_runtime_errorf(ctx, "package import paths cannot contain NUL bytes\n");
    } else {
        status = tf_package_load(ctx, path->str.ptr, TF_ROOT_PACKAGE,
                                 alias->str.ptr, alias->str.len, NULL);
    }
    tf_obj_release(path);
    tf_obj_release(alias);
    return status;
}

tf_ret tf_nan_q(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *o = tf_stack_pop(ctx);
    bool result = tf_obj_typeof(o) == TF_OBJ_TYPE_FLOAT && isnan(o->f);
    tf_stack_push(ctx, tf_obj_new_bool(result));
    tf_obj_release(o);
    return TF_OK;
}

tf_ret tf_inf_q(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *o = tf_stack_pop(ctx);
    bool result = tf_obj_typeof(o) == TF_OBJ_TYPE_FLOAT && isinf(o->f);
    tf_stack_push(ctx, tf_obj_new_bool(result));
    tf_obj_release(o);
    return TF_OK;
}

tf_ret tf_word_q(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *o = tf_stack_pop(ctx);
    bool result = tf_obj_typeof(o) == TF_OBJ_TYPE_SYMBOL &&
                  tf_dict_lookup(ctx, o) != NULL;
    tf_stack_push(ctx, tf_obj_new_bool(result));
    tf_obj_release(o);
    return TF_OK;
}

tf_ret tf_var_q(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *o = tf_stack_pop(ctx);
    bool result =
        tf_obj_typeof(o) == TF_OBJ_TYPE_SYMBOL &&
        tf_scope_lookup_var(ctx, o) != NULL;
    tf_stack_push(ctx, tf_obj_new_bool(result));
    tf_obj_release(o);
    return TF_OK;
}

tf_ret tf_inf(tf_ctx *ctx) {
    tf_stack_push(ctx, tf_obj_new_float(INFINITY));
    return TF_OK;
}

tf_ret tf_nan(tf_ctx *ctx) {
    tf_stack_push(ctx, tf_obj_new_float(NAN));
    return TF_OK;
}

tf_ret tf_body(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_SYMBOL)) return TF_ERR;
    tf_obj *name = tf_stack_pop_type(ctx, TF_OBJ_TYPE_SYMBOL);

    tf_word *f = tf_dict_lookup(ctx, name);
    if (!f || f->type != TF_WORD_USER) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected a user-defined word, found '%s'\n",
                              ctx->current_word, name->str.ptr);
        tf_obj_release(name);
        return TF_ERR;
    }

    tf_stack_push(ctx, f->user_impl);
    tf_obj_retain(f->user_impl);
    tf_obj_release(name);
    return TF_OK;
}

tf_ret tf_to_symbol(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *value = tf_stack_peek(ctx, 0);
    tf_type type = tf_obj_typeof(value);
    if (type != TF_OBJ_TYPE_STR && type != TF_OBJ_TYPE_CALL) {
        tf_ctx_runtime_errorf(ctx,
                              "'%s' expected string or call at stack depth 0, "
                              "found %s\n",
                              ctx->current_word, tf_obj_type_name(value));
        return TF_ERR;
    }
    value = tf_stack_pop(ctx);

    tf_obj *symbol = tf_obj_new_symbol(value->str.ptr, value->str.len);
    tf_obj_set_span(symbol, value->span);
    tf_stack_push(ctx, symbol);
    tf_obj_release(value);
    return TF_OK;
}

tf_ret tf_to_call(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_SYMBOL)) return TF_ERR;
    tf_obj *symbol = tf_stack_pop_type(ctx, TF_OBJ_TYPE_SYMBOL);

    tf_obj *call = tf_obj_new_call(symbol->str.ptr, symbol->str.len);
    tf_obj_set_span(call, symbol->span);
    tf_stack_push(ctx, call);
    tf_obj_release(symbol);
    return TF_OK;
}

tf_ret tf_name(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_SYMBOL)) return TF_ERR;
    tf_obj *sym = tf_stack_pop_type(ctx, TF_OBJ_TYPE_SYMBOL);

    tf_stack_push(ctx, tf_obj_new_string(sym->str.ptr, sym->str.len));
    tf_obj_release(sym);
    return TF_OK;
}

static tf_ret type_check(tf_ctx *ctx, tf_type type) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *o = tf_stack_pop(ctx);
    tf_stack_push(ctx, tf_obj_new_bool(tf_obj_typeof(o) == type));
    tf_obj_release(o);
    return TF_OK;
}

tf_ret tf_bool_q(tf_ctx *ctx) {
    return type_check(ctx, TF_OBJ_TYPE_BOOL);
}
tf_ret tf_int_q(tf_ctx *ctx) {
    return type_check(ctx, TF_OBJ_TYPE_INT);
}
tf_ret tf_float_q(tf_ctx *ctx) {
    return type_check(ctx, TF_OBJ_TYPE_FLOAT);
}
tf_ret tf_string_q(tf_ctx *ctx) {
    return type_check(ctx, TF_OBJ_TYPE_STR);
}
tf_ret tf_symbol_q(tf_ctx *ctx) {
    return type_check(ctx, TF_OBJ_TYPE_SYMBOL);
}
tf_ret tf_call_q(tf_ctx *ctx) {
    return type_check(ctx, TF_OBJ_TYPE_CALL);
}
tf_ret tf_vector_q(tf_ctx *ctx) {
    return type_check(ctx, TF_OBJ_TYPE_VECTOR);
}
tf_ret tf_list_q(tf_ctx *ctx) {
    return type_check(ctx, TF_OBJ_TYPE_LIST);
}
tf_ret tf_map_q(tf_ctx *ctx) {
    return type_check(ctx, TF_OBJ_TYPE_MAP);
}
tf_ret tf_set_q(tf_ctx *ctx) {
    return type_check(ctx, TF_OBJ_TYPE_SET);
}
tf_ret tf_deque_q(tf_ctx *ctx) {
    return type_check(ctx, TF_OBJ_TYPE_DEQUE);
}
tf_ret tf_pqueue_q(tf_ctx *ctx) {
    return type_check(ctx, TF_OBJ_TYPE_PQUEUE);
}
tf_ret tf_resource_q(tf_ctx *ctx) {
    return type_check(ctx, TF_OBJ_TYPE_RESOURCE);
}

tf_ret tf_typeof(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *o = tf_stack_pop(ctx);
    const char *type_str = "unknown";
    switch (tf_obj_typeof(o)) {
    case TF_OBJ_TYPE_BOOL:
        type_str = "bool";
        break;
    case TF_OBJ_TYPE_INT:
        type_str = "int";
        break;
    case TF_OBJ_TYPE_FLOAT:
        type_str = "float";
        break;
    case TF_OBJ_TYPE_STR:
        type_str = "string";
        break;
    case TF_OBJ_TYPE_SYMBOL:
        type_str = "symbol";
        break;
    case TF_OBJ_TYPE_CALL:
        type_str = "call";
        break;
    case TF_OBJ_TYPE_VECTOR:
        type_str = "vector";
        break;
    case TF_OBJ_TYPE_LIST:
        type_str = "list";
        break;
    case TF_OBJ_TYPE_MAP:
        type_str = "map";
        break;
    case TF_OBJ_TYPE_SET:
        type_str = "set";
        break;
    case TF_OBJ_TYPE_DEQUE:
        type_str = "deque";
        break;
    case TF_OBJ_TYPE_PQUEUE:
        type_str = "pqueue";
        break;
    case TF_OBJ_TYPE_RESOURCE:
        type_str = "resource";
        break;
    case TF_OBJ_TYPE_VARLIST:
        type_str = "varlist";
        break;
    case TF_OBJ_TYPE_VARFETCH:
        type_str = "varfetch";
        break;
    }
    tf_stack_push(ctx, tf_obj_new_string((char *)type_str, strlen(type_str)));
    tf_obj_release(o);
    return TF_OK;
}

typedef struct {
    char *name;
    size_t name_len;
    tf_word *word;
} word_view;

typedef struct {
    word_view *items;
    size_t len;
    size_t cap;
} word_view_table;

static int word_view_cmp(const void *a, const void *b) {
    const word_view *left = a;
    const word_view *right = b;
    size_t min_len = left->name_len < right->name_len ? left->name_len
                                                       : right->name_len;
    int cmp = memcmp(left->name, right->name, min_len);
    if (cmp != 0) return cmp;
    return (left->name_len > right->name_len) -
           (left->name_len < right->name_len);
}

static void collect_word_view(const char *display_name,
                              size_t display_name_len, tf_word *word,
                              void *userdata) {
    word_view_table *views = userdata;
    if (views->len >= views->cap) {
        views->cap = views->cap ? views->cap * 2 : 64;
        views->items = tf_xrealloc(views->items,
                                   sizeof(word_view) * views->cap);
    }
    char *name = tf_xmalloc(display_name_len + 1);
    memcpy(name, display_name, display_name_len);
    name[display_name_len] = '\0';
    views->items[views->len++] =
        (word_view){name, display_name_len, word};
}

static void word_views_dispose(word_view_table *views) {
    for (size_t i = 0; i < views->len; i++) free(views->items[i].name);
    free(views->items);
}

typedef struct {
    char *ptr;
    size_t len;
    size_t cap;
} strbuf;

static void strbuf_init(strbuf *buf) {
    buf->len = 0;
    buf->cap = 64;
    buf->ptr = tf_xmalloc(buf->cap);
    buf->ptr[0] = '\0';
}

static void strbuf_reserve(strbuf *buf, size_t extra) {
    size_t needed = buf->len + extra + 1;
    while (needed > buf->cap) buf->cap *= 2;
    buf->ptr = tf_xrealloc(buf->ptr, buf->cap);
}

static void strbuf_append_mem(strbuf *buf, const char *s, size_t len) {
    strbuf_reserve(buf, len);
    memcpy(buf->ptr + buf->len, s, len);
    buf->len += len;
    buf->ptr[buf->len] = '\0';
}

static void strbuf_append_cstr(strbuf *buf, const char *s) {
    strbuf_append_mem(buf, s, strlen(s));
}

static void strbuf_append_char(strbuf *buf, char c) {
    strbuf_reserve(buf, 1);
    buf->ptr[buf->len++] = c;
    buf->ptr[buf->len] = '\0';
}

static void strbuf_append_escaped(strbuf *buf, const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '\\':
            strbuf_append_cstr(buf, "\\\\");
            break;
        case '"':
            strbuf_append_cstr(buf, "\\\"");
            break;
        case '\n':
            strbuf_append_cstr(buf, "\\n");
            break;
        case '\r':
            strbuf_append_cstr(buf, "\\r");
            break;
        case '\t':
            strbuf_append_cstr(buf, "\\t");
            break;
        default: {
            if (c < 0x20 || c >= 0x7f) {
                char escaped[5];
                snprintf(escaped, sizeof escaped, "\\x%02x", c);
                strbuf_append_cstr(buf, escaped);
            } else {
                strbuf_append_char(buf, (char)c);
            }
            break;
        }
        }
    }
}

static void strbuf_append_source_obj(strbuf *buf, tf_obj *o) {
    char num_buf[64];

    switch (tf_obj_typeof(o)) {
    case TF_OBJ_TYPE_INT:
        snprintf(num_buf, sizeof num_buf, "%" PRId64,
                 tf_obj_int_value(o));
        strbuf_append_cstr(buf, num_buf);
        break;
    case TF_OBJ_TYPE_FLOAT:
        tf_format_double(num_buf, sizeof num_buf, o->f);
        strbuf_append_cstr(buf, num_buf);
        if (isfinite(o->f) && !strpbrk(num_buf, ".eE")) {
            strbuf_append_cstr(buf, ".0");
        }
        break;
    case TF_OBJ_TYPE_BOOL:
        strbuf_append_cstr(buf, o->b ? "true" : "false");
        break;
    case TF_OBJ_TYPE_STR:
        strbuf_append_char(buf, '"');
        strbuf_append_escaped(buf, o->str.ptr, o->str.len);
        strbuf_append_char(buf, '"');
        break;
    case TF_OBJ_TYPE_SYMBOL:
        strbuf_append_char(buf, '\'');
        strbuf_append_mem(buf, o->str.ptr, o->str.len);
        break;
    case TF_OBJ_TYPE_CALL:
        strbuf_append_mem(buf, o->str.ptr, o->str.len);
        break;
    case TF_OBJ_TYPE_VARFETCH:
        strbuf_append_char(buf, '$');
        strbuf_append_mem(buf, o->str.ptr, o->str.len);
        break;
    case TF_OBJ_TYPE_VARLIST:
        strbuf_append_char(buf, '|');
        for (size_t i = 0; i < o->vector.len; i++) {
            if (i > 0) strbuf_append_char(buf, ' ');
            strbuf_append_mem(buf, o->vector.elem[i]->str.ptr,
                              o->vector.elem[i]->str.len);
        }
        strbuf_append_char(buf, '|');
        break;
    case TF_OBJ_TYPE_VECTOR:
        strbuf_append_char(buf, '[');
        for (size_t i = 0; i < o->vector.len; i++) {
            if (i > 0) strbuf_append_char(buf, ' ');
            strbuf_append_source_obj(buf, o->vector.elem[i]);
        }
        strbuf_append_char(buf, ']');
        break;
    case TF_OBJ_TYPE_LIST: {
        strbuf_append_char(buf, '(');
        tf_list_node *node = o->list.head;
        while (node) {
            strbuf_append_source_obj(buf, node->value);
            node = node->next;
            if (node) strbuf_append_char(buf, ' ');
        }
        strbuf_append_char(buf, ')');
        break;
    }
    case TF_OBJ_TYPE_MAP:
        strbuf_append_char(buf, '{');
        for (size_t i = 0; i < o->map.len; i++) {
            if (i > 0) strbuf_append_char(buf, ' ');
            strbuf_append_source_obj(buf, o->map.entries[i].key);
            strbuf_append_char(buf, ' ');
            strbuf_append_source_obj(buf, o->map.entries[i].value);
        }
        strbuf_append_char(buf, '}');
        break;
    case TF_OBJ_TYPE_SET:
        strbuf_append_cstr(buf, "#{");
        for (size_t i = 0; i < o->set.len; i++) {
            if (i > 0) strbuf_append_char(buf, ' ');
            strbuf_append_source_obj(buf, o->set.entries[i].item);
        }
        strbuf_append_char(buf, '}');
        break;
    case TF_OBJ_TYPE_DEQUE:
        strbuf_append_char(buf, '[');
        for (size_t i = 0; i < o->deque.len; i++) {
            if (i > 0) strbuf_append_char(buf, ' ');
            strbuf_append_source_obj(buf, tf_deque_get(o, i));
        }
        strbuf_append_cstr(buf, "] >deque");
        break;
    case TF_OBJ_TYPE_PQUEUE: {
        strbuf_append_char(buf, '[');
        tf_obj *tmp = tf_pqueue_clone(o);
        for (size_t i = 0; tmp->pqueue.len > 0; i++) {
            tf_obj *priority = NULL;
            tf_obj *value = NULL;
            tf_pqueue_pop(tmp, &priority, &value);
            if (i > 0) strbuf_append_char(buf, ' ');
            strbuf_append_cstr(buf, "[");
            strbuf_append_source_obj(buf, priority);
            strbuf_append_char(buf, ' ');
            strbuf_append_source_obj(buf, value);
            strbuf_append_char(buf, ']');
            tf_obj_release(priority);
            tf_obj_release(value);
        }
        tf_obj_release(tmp);
        strbuf_append_cstr(buf, "] >pqueue");
        break;
    }
    case TF_OBJ_TYPE_RESOURCE:
        strbuf_append_cstr(buf, "<resource:");
        strbuf_append_mem(buf, o->resource.type_name, o->resource.type_len);
        strbuf_append_char(buf, '>');
        break;
    }
}

tf_ret tf_repr(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *value = tf_stack_pop(ctx);
    strbuf buf;
    strbuf_init(&buf);
    strbuf_append_source_obj(&buf, value);
    tf_stack_push(ctx, tf_obj_new_string_take(buf.ptr, buf.len));
    tf_obj_release(value);
    return TF_OK;
}

static void strbuf_append_word_source(strbuf *buf, tf_word *word) {
    if (word->type == TF_WORD_NATIVE) {
        strbuf_append_mem(buf, word->name, word->name_len);
        strbuf_append_cstr(buf, " is a native word");
        return;
    }

    strbuf_append_char(buf, '\'');
    strbuf_append_mem(buf, word->name, word->name_len);
    strbuf_append_cstr(buf, " [");
    for (size_t i = 0; i < word->user_impl->vector.len; i++) {
        strbuf_append_char(buf, ' ');
        strbuf_append_source_obj(buf, word->user_impl->vector.elem[i]);
    }
    strbuf_append_cstr(buf, " ] def");
}

static int ascii_lower(int c) {
    return tolower((unsigned char)c);
}

static bool contains_ascii_casefold(const char *haystack, size_t haystack_len,
                                    const char *needle, size_t needle_len) {
    if (needle_len == 0) return true;
    if (haystack_len < needle_len) return false;

    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        bool match = true;
        for (size_t j = 0; j < needle_len; j++) {
            if (ascii_lower(haystack[i + j]) != ascii_lower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

static const tf_doc_entry *word_doc(tf_word *word, tf_doc_entry *scratch) {
    if (!word || word->type != TF_WORD_NATIVE) return NULL;
    if (word->package_index == TF_ROOT_PACKAGE) {
        return tf_doc_lookup(word->name);
    }
    if (!word->doc_stack_effect && !word->doc_description) return NULL;
    *scratch = (tf_doc_entry){
        word->name,
        word->doc_stack_effect ? word->doc_stack_effect : "",
        "",
        word->doc_description ? word->doc_description : "",
    };
    return scratch;
}

static bool word_matches_query(tf_word *word, const char *query,
                               size_t query_len) {
    if (contains_ascii_casefold(word->name, word->name_len, query,
                                query_len)) {
        return true;
    }

    tf_doc_entry scratch;
    const tf_doc_entry *doc = word_doc(word, &scratch);
    return doc &&
           (contains_ascii_casefold(doc->stack_effect, strlen(doc->stack_effect),
                                    query, query_len) ||
            contains_ascii_casefold(doc->syntax, strlen(doc->syntax),
                                    query, query_len) ||
            contains_ascii_casefold(doc->description, strlen(doc->description),
                                    query, query_len));
}

tf_ret tf_words(tf_ctx *ctx) {
    word_view_table views = {0};
    tf_dict_each_visible(ctx, collect_word_view, &views);
    qsort(views.items, views.len, sizeof(word_view), word_view_cmp);
    tf_obj *result = tf_obj_new_vector_with_capacity(views.len);
    for (size_t i = 0; i < views.len; i++) {
        tf_vector_push(result, tf_obj_new_symbol(views.items[i].name,
                                                views.items[i].name_len));
    }
    word_views_dispose(&views);
    tf_stack_push(ctx, result);
    return TF_OK;
}

tf_ret tf_see(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_SYMBOL)) return TF_ERR;
    tf_obj *name = tf_stack_pop_type(ctx, TF_OBJ_TYPE_SYMBOL);

    tf_word *word = tf_dict_lookup(ctx, name);
    if (!word) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected a defined word, found '%s'\n",
                              ctx->current_word, name->str.ptr);
        tf_obj_release(name);
        return TF_ERR;
    }

    strbuf buf;
    strbuf_init(&buf);
    strbuf_append_word_source(&buf, word);

    tf_stack_push(ctx, tf_obj_new_string_take(buf.ptr, buf.len));
    tf_obj_release(name);
    return TF_OK;
}

tf_ret tf_doc(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_SYMBOL)) return TF_ERR;
    tf_obj *name = tf_stack_pop_type(ctx, TF_OBJ_TYPE_SYMBOL);

    tf_word *word = tf_dict_lookup(ctx, name);
    if (!word) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected a defined word, found '%s'\n",
                              ctx->current_word, name->str.ptr);
        tf_obj_release(name);
        return TF_ERR;
    }

    strbuf buf;
    strbuf_init(&buf);
    tf_doc_entry scratch;
    const tf_doc_entry *entry = word_doc(word, &scratch);
    if (entry) {
        strbuf_append_cstr(&buf, name->str.ptr);
        if (entry->stack_effect[0] != '\0') {
            strbuf_append_cstr(&buf, "\nstack: ");
            strbuf_append_cstr(&buf, entry->stack_effect);
        }
        if (entry->syntax[0] != '\0') {
            strbuf_append_cstr(&buf, "\nsyntax: ");
            strbuf_append_cstr(&buf, entry->syntax);
        }
        if (entry->description[0] != '\0') strbuf_append_char(&buf, '\n');
        strbuf_append_cstr(&buf, entry->description);
    } else {
        strbuf_append_word_source(&buf, word);
    }

    tf_stack_push(ctx, tf_obj_new_string_take(buf.ptr, buf.len));
    tf_obj_release(name);
    return TF_OK;
}

tf_ret tf_apropos(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *query = tf_stack_peek(ctx, 0);
    tf_type query_type = tf_obj_typeof(query);
    if (query_type != TF_OBJ_TYPE_STR && query_type != TF_OBJ_TYPE_SYMBOL) {
        tf_ctx_runtime_errorf(
            ctx, "'%s' expected string or symbol at stack depth 0, found %s\n",
            ctx->current_word, tf_obj_type_name(query));
        return TF_ERR;
    }
    query = tf_stack_pop(ctx);

    word_view_table views = {0};
    tf_dict_each_visible(ctx, collect_word_view, &views);
    size_t match_count = 0;
    for (size_t i = 0; i < views.len; i++) {
        word_view *view = &views.items[i];
        if (!word_matches_query(view->word, query->str.ptr, query->str.len) &&
            !contains_ascii_casefold(view->name, view->name_len,
                                     query->str.ptr, query->str.len)) {
            free(view->name);
            continue;
        }
        views.items[match_count++] = *view;
    }
    views.len = match_count;
    qsort(views.items, views.len, sizeof(word_view), word_view_cmp);

    tf_obj *result = tf_obj_new_vector_with_capacity(views.len);
    for (size_t i = 0; i < views.len; i++) {
        tf_vector_push(result, tf_obj_new_symbol(views.items[i].name,
                                                views.items[i].name_len));
    }

    word_views_dispose(&views);
    tf_obj_release(query);
    tf_stack_push(ctx, result);
    return TF_OK;
}

tf_ret tf_def(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 2)) return TF_ERR;
    tf_obj *body = tf_stack_peek(ctx, 0);
    tf_obj *word_name = tf_stack_peek(ctx, 1);
    if (tf_obj_typeof(word_name) != TF_OBJ_TYPE_SYMBOL) {
        tf_ctx_runtime_errorf(ctx, "'def' expected symbol at stack depth 1, found %s\n",
                              tf_obj_type_name(word_name));
        return TF_ERR;
    }
    if (tf_obj_typeof(body) != TF_OBJ_TYPE_VECTOR) {
        tf_ctx_runtime_errorf(ctx, "'def' expected vector at stack depth 0, found %s\n",
                              tf_obj_type_name(body));
        return TF_ERR;
    }

    body = tf_stack_pop(ctx);
    word_name = tf_stack_pop(ctx);
    bool defined = tf_dict_set_user(ctx, word_name, body);

    tf_obj_release(body);
    tf_obj_release(word_name);
    return defined ? TF_OK : TF_ERR;
}

tf_ret tf_private(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_SYMBOL)) return TF_ERR;
    tf_obj *name = tf_stack_pop_type(ctx, TF_OBJ_TYPE_SYMBOL);
    bool hidden = tf_dict_make_private(ctx, name);
    tf_obj_release(name);
    return hidden ? TF_OK : TF_ERR;
}

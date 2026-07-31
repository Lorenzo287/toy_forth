#include "tf_builtins.h"
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "tf_alloc.h"
#include "tf_exec.h"
#include "tf_obj.h"

static bool is_char_obj(tf_obj *o) {
    return tf_obj_typeof(o) == TF_OBJ_TYPE_STR && o->str.len == 1;
}

static bool is_countable(tf_obj *o) {
    tf_type type = tf_obj_typeof(o);
    return type == TF_OBJ_TYPE_VECTOR || type == TF_OBJ_TYPE_LIST ||
           type == TF_OBJ_TYPE_STR || type == TF_OBJ_TYPE_MAP ||
           type == TF_OBJ_TYPE_SET || type == TF_OBJ_TYPE_DEQUE ||
           type == TF_OBJ_TYPE_PQUEUE;
}

static bool is_indexed_obj(tf_obj *o) {
    tf_type type = tf_obj_typeof(o);
    return type == TF_OBJ_TYPE_VECTOR || type == TF_OBJ_TYPE_STR;
}

static bool require_indexed(tf_ctx *ctx, size_t depth) {
    if (!tf_ctx_require_stack(ctx, depth + 1)) return false;
    tf_obj *o = tf_stack_peek(ctx, depth);
    if (is_indexed_obj(o)) return true;

    tf_ctx_runtime_errorf(ctx,
                          "'%s' expected indexed collection at stack depth %zu, found %s\n",
                          ctx->current_word, depth, tf_obj_type_name(o));
    return false;
}

static size_t sequence_len(tf_obj *seq) {
    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_VECTOR) return seq->vector.len;
    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_LIST) return seq->list.len;
    return seq->str.len;
}

static void push_sequence_items_to_vector(tf_obj *dest, tf_obj *seq,
                                          size_t start, size_t end) {
    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_VECTOR) {
        for (size_t i = start; i < end; i++) {
            tf_obj *item = seq->vector.elem[i];
            tf_obj_retain(item);
            tf_vector_push(dest, item);
        }
        return;
    }

    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_STR) {
        for (size_t i = start; i < end; i++) {
            tf_vector_push(dest, tf_obj_new_string(seq->str.ptr + i, 1));
        }
        return;
    }

    tf_list_node *node = seq->list.head;
    for (size_t i = 0; i < start && node; i++) node = node->next;
    for (size_t i = start; i < end && node; i++, node = node->next) {
        tf_obj_retain(node->value);
        tf_vector_push(dest, node->value);
    }
}

static tf_obj *vector_to_sequence_family(tf_obj *items, tf_obj *like) {
    if (tf_obj_typeof(like) == TF_OBJ_TYPE_LIST) {
        return tf_list_from_vector(items);
    }
    return items;
}

static tf_obj *sequence_to_vector(tf_obj *seq) {
    size_t len = sequence_len(seq);
    tf_obj *items = tf_obj_new_vector_with_capacity(len);
    push_sequence_items_to_vector(items, seq, 0, len);
    return items;
}

static bool vector_contains_equal(tf_obj *vector, tf_obj *needle) {
    for (size_t i = 0; i < vector->vector.len; i++) {
        if (tf_obj_equal(vector->vector.elem[i], needle)) return true;
    }
    return false;
}

static int64_t sequence_indexof(tf_obj *seq, tf_obj *needle) {
    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_STR) {
        if (needle->str.len == 0) return 0;
        if (needle->str.len > seq->str.len) return -1;
        size_t last = seq->str.len - needle->str.len;
        for (size_t i = 0; i <= last; i++) {
            if (memcmp(seq->str.ptr + i, needle->str.ptr,
                       needle->str.len) == 0) {
                return (int64_t)i;
            }
        }
        return -1;
    }

    tf_sequence_iter iter;
    tf_sequence_iter_init(&iter, seq);
    for (size_t i = 0;; i++) {
        tf_obj *item = tf_sequence_iter_next_owned(&iter);
        if (!item) break;
        bool found = tf_obj_equal(item, needle);
        tf_obj_release(item);
        if (found) return (int64_t)i;
    }
    return -1;
}

typedef enum {
    TF_SORT_NUMERIC,
    TF_SORT_STRING
} natural_sort_kind;

#define TF_SORT_INSERTION_CUTOFF 16
#define TF_STRING_COUNTING_CUTOFF 64
#define TF_UNIQUE_HASH_CUTOFF 16

static bool validate_natural_sort(tf_ctx *ctx, tf_obj *items,
                                  natural_sort_kind *kind) {
    if (items->vector.len == 0) {
        *kind = TF_SORT_NUMERIC;
        return true;
    }

    tf_obj *first = items->vector.elem[0];
    tf_type first_type = tf_obj_typeof(first);
    bool numeric =
        first_type == TF_OBJ_TYPE_INT || first_type == TF_OBJ_TYPE_FLOAT;
    if (!numeric && first_type != TF_OBJ_TYPE_STR) {
        tf_ctx_runtime_errorf(ctx, "'%s' cannot order values of type %s\n",
                              ctx->current_word, tf_obj_type_name(first));
        return false;
    }
    *kind = numeric ? TF_SORT_NUMERIC : TF_SORT_STRING;

    for (size_t i = 0; i < items->vector.len; i++) {
        tf_obj *item = items->vector.elem[i];
        tf_type item_type = tf_obj_typeof(item);
        bool item_numeric =
            item_type == TF_OBJ_TYPE_INT || item_type == TF_OBJ_TYPE_FLOAT;
        if ((*kind == TF_SORT_NUMERIC && !item_numeric) ||
            (*kind == TF_SORT_STRING && item_type != TF_OBJ_TYPE_STR)) {
            tf_ctx_runtime_errorf(ctx, "'%s' cannot compare %s with %s\n",
                                  ctx->current_word, tf_obj_type_name(first),
                                  tf_obj_type_name(item));
            return false;
        }
        if (item_type == TF_OBJ_TYPE_FLOAT && isnan(item->f)) {
            tf_ctx_runtime_errorf(ctx, "'%s' cannot order NaN values\n",
                                  ctx->current_word);
            return false;
        }
    }
    return true;
}

static int natural_compare(tf_obj *a, tf_obj *b, natural_sort_kind kind) {
    if (kind == TF_SORT_STRING) return tf_obj_compare_string(a, b);
    return tf_obj_compare_number(a, b);
}

static void insertion_sort_items(tf_obj **items, size_t start, size_t end,
                                 natural_sort_kind kind) {
    for (size_t i = start + 1; i < end; i++) {
        tf_obj *key = items[i];
        size_t j = i;
        while (j > start && natural_compare(items[j - 1], key, kind) > 0) {
            items[j] = items[j - 1];
            j--;
        }
        items[j] = key;
    }
}

static void merge_sorted_runs(tf_obj **source, tf_obj **dest, size_t left,
                              size_t mid, size_t right,
                              natural_sort_kind kind) {
    size_t i = left;
    size_t j = mid;
    size_t out = left;
    while (i < mid && j < right) {
        if (natural_compare(source[i], source[j], kind) <= 0) {
            dest[out++] = source[i++];
        } else {
            dest[out++] = source[j++];
        }
    }
    while (i < mid) dest[out++] = source[i++];
    while (j < right) dest[out++] = source[j++];
}

static bool sort_vector_natural(tf_ctx *ctx, tf_obj *items) {
    natural_sort_kind kind;
    if (!validate_natural_sort(ctx, items, &kind)) return false;

    size_t len = items->vector.len;
    bool already_sorted = true;
    for (size_t i = 1; i < len; i++) {
        if (natural_compare(items->vector.elem[i - 1],
                            items->vector.elem[i], kind) > 0) {
            already_sorted = false;
            break;
        }
    }
    if (already_sorted) return true;

    for (size_t start = 0; start < len; start += TF_SORT_INSERTION_CUTOFF) {
        size_t end = start + TF_SORT_INSERTION_CUTOFF;
        if (end > len) end = len;
        insertion_sort_items(items->vector.elem, start, end, kind);
    }
    if (len <= TF_SORT_INSERTION_CUTOFF) return true;

    tf_obj **buffer = tf_xmalloc(sizeof(tf_obj *) * len);
    tf_obj **source = items->vector.elem;
    tf_obj **dest = buffer;
    size_t width = TF_SORT_INSERTION_CUTOFF;
    while (width < len) {
        for (size_t left = 0; left < len; left += width * 2) {
            size_t mid = left + width;
            if (mid > len) mid = len;
            size_t right = mid + width;
            if (right > len) right = len;
            merge_sorted_runs(source, dest, left, mid, right, kind);
        }
        tf_obj **tmp = source;
        source = dest;
        dest = tmp;
        if (width > len / 2) {
            width = len;
        } else {
            width *= 2;
        }
    }

    if (source != items->vector.elem) {
        memcpy(items->vector.elem, source, sizeof(tf_obj *) * len);
    }
    free(buffer);
    return true;
}

static void sort_string_bytes(char *buf, size_t len) {
    if (len <= TF_STRING_COUNTING_CUTOFF) {
        for (size_t i = 1; i < len; i++) {
            unsigned char key = (unsigned char)buf[i];
            size_t j = i;
            while (j > 0 && (unsigned char)buf[j - 1] > key) {
                buf[j] = buf[j - 1];
                j--;
            }
            buf[j] = (char)key;
        }
        return;
    }

    size_t counts[256] = {0};
    for (size_t i = 0; i < len; i++) counts[(unsigned char)buf[i]]++;
    size_t out = 0;
    for (size_t value = 0; value < 256; value++) {
        for (size_t count = counts[value]; count > 0; count--) {
            buf[out++] = (char)value;
        }
    }
}

static bool pair_items(tf_obj *pair, tf_obj **first, tf_obj **second) {
    if (tf_obj_typeof(pair) == TF_OBJ_TYPE_VECTOR && pair->vector.len == 2) {
        *first = pair->vector.elem[0];
        *second = pair->vector.elem[1];
        return true;
    }
    if (tf_obj_typeof(pair) == TF_OBJ_TYPE_LIST && pair->list.len == 2) {
        *first = pair->list.head->value;
        *second = pair->list.head->next->value;
        return true;
    }
    return false;
}

static void report_pair_error(tf_ctx *ctx, size_t index, tf_obj *pair) {
    if (tf_obj_typeof(pair) == TF_OBJ_TYPE_VECTOR) {
        tf_ctx_runtime_errorf(
            ctx,
            "'%s' expected item %zu to be a two-item vector or list, found vector of length %zu\n",
            ctx->current_word, index, pair->vector.len);
        return;
    }
    if (tf_obj_typeof(pair) == TF_OBJ_TYPE_LIST) {
        tf_ctx_runtime_errorf(
            ctx,
            "'%s' expected item %zu to be a two-item vector or list, found list of length %zu\n",
            ctx->current_word, index, pair->list.len);
        return;
    }
    tf_ctx_runtime_errorf(
        ctx,
        "'%s' expected item %zu to be a two-item vector or list, found %s\n",
        ctx->current_word, index, tf_obj_type_name(pair));
}

static bool require_countable(tf_ctx *ctx, size_t depth) {
    if (!tf_ctx_require_stack(ctx, depth + 1)) return false;
    tf_obj *o = tf_stack_peek(ctx, depth);
    if (is_countable(o)) return true;

    tf_ctx_runtime_errorf(ctx,
                          "'%s' expected finite collection at stack depth %zu, found %s\n",
                          ctx->current_word, depth, tf_obj_type_name(o));
    return false;
}

static bool require_char(tf_ctx *ctx, size_t depth) {
    if (!tf_ctx_require_stack(ctx, depth + 1)) return false;
    tf_obj *o = tf_stack_peek(ctx, depth);
    if (is_char_obj(o)) return true;

    tf_ctx_runtime_errorf(ctx, "'%s' expected one-character string at stack depth %zu, found %s\n",
                          ctx->current_word, depth, tf_obj_type_name(o));
    return false;
}

static bool index_in_bounds(tf_ctx *ctx, int64_t idx, size_t len) {
    if (idx >= 0 && (uint64_t)idx < len) return true;
    tf_ctx_runtime_errorf(ctx, "'%s' index %" PRId64
                               " is out of range for length %zu\n",
                          ctx->current_word, idx, len);
    return false;
}

typedef enum {
    TF_CHAR_PRED_LETTER,
    TF_CHAR_PRED_DIGIT,
    TF_CHAR_PRED_ALNUM,
    TF_CHAR_PRED_SPACE,
    TF_CHAR_PRED_UPPER,
    TF_CHAR_PRED_LOWER,
    TF_CHAR_PRED_PUNCT
} char_pred;

static bool match_char_pred(unsigned char c, char_pred pred) {
    switch (pred) {
    case TF_CHAR_PRED_LETTER:
        return isalpha(c) != 0;
    case TF_CHAR_PRED_DIGIT:
        return isdigit(c) != 0;
    case TF_CHAR_PRED_ALNUM:
        return isalnum(c) != 0;
    case TF_CHAR_PRED_SPACE:
        return isspace(c) != 0;
    case TF_CHAR_PRED_UPPER:
        return isupper(c) != 0;
    case TF_CHAR_PRED_LOWER:
        return islower(c) != 0;
    case TF_CHAR_PRED_PUNCT:
        return ispunct(c) != 0;
    }
    return false;
}

static tf_ret char_predicate(tf_ctx *ctx, char_pred pred) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *o = tf_stack_pop(ctx);
    bool result = is_char_obj(o) &&
                  match_char_pred((unsigned char)o->str.ptr[0], pred);
    tf_stack_push(ctx, tf_obj_new_bool(result));
    tf_obj_release(o);
    return TF_OK;
}

static char *find_mem(char *haystack, size_t haystack_len,
                      const char *needle, size_t needle_len) {
    if (needle_len == 0) return haystack;
    if (needle_len > haystack_len) return NULL;
    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return haystack + i;
        }
    }
    return NULL;
}

static bool require_set_pair(tf_ctx *ctx) {
    return tf_ctx_require_stack(ctx, 2) &&
           tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_SET) &&
           tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_SET);
}

static bool set_is_subset(tf_obj *left, tf_obj *right) {
    if (left->set.len > right->set.len) return false;
    for (size_t i = 0; i < left->set.len; i++) {
        if (!tf_set_has(right, left->set.entries[i].item)) return false;
    }
    return true;
}

static bool sets_are_disjoint(tf_obj *left, tf_obj *right) {
    tf_obj *smaller = left->set.len <= right->set.len ? left : right;
    tf_obj *larger = smaller == left ? right : left;
    for (size_t i = 0; i < smaller->set.len; i++) {
        if (tf_set_has(larger, smaller->set.entries[i].item)) return false;
    }
    return true;
}

static tf_obj *set_select(tf_obj *source, tf_obj *membership,
                          bool keep_present) {
    tf_obj *result = tf_obj_new_set();
    for (size_t i = 0; i < source->set.len; i++) {
        tf_obj *item = source->set.entries[i].item;
        if (tf_set_has(membership, item) == keep_present) {
            tf_set_add(result, item);
        }
    }
    return result;
}

static bool valid_priority(tf_ctx *ctx, tf_obj *priority) {
    double value = 0;
    if (tf_obj_typeof(priority) == TF_OBJ_TYPE_INT) {
        value = (double)tf_obj_int_value(priority);
    } else if (tf_obj_typeof(priority) == TF_OBJ_TYPE_FLOAT) {
        value = priority->f;
    } else {
        tf_ctx_runtime_errorf(ctx, "'%s' expected numeric priority, found %s\n",
                              ctx->current_word, tf_obj_type_name(priority));
        return false;
    }

    if (!isfinite(value)) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected finite priority\n",
                              ctx->current_word);
        return false;
    }
    return true;
}

tf_ret tf_at(tf_ctx *ctx) {
    if (!require_indexed(ctx, 1) ||
        !tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_INT)) {
        return TF_ERR;
    }
    tf_obj *idx_obj = tf_stack_peek(ctx, 0);
    tf_obj *coll_obj = tf_stack_peek(ctx, 1);

    int64_t idx = tf_obj_int_value(idx_obj);
    if (tf_obj_typeof(coll_obj) == TF_OBJ_TYPE_VECTOR) {
        if (!index_in_bounds(ctx, idx, coll_obj->vector.len)) return TF_ERR;
        idx_obj = tf_stack_pop(ctx);
        coll_obj = tf_stack_pop(ctx);
        tf_obj *result = coll_obj->vector.elem[(size_t)idx];
        tf_obj_retain(result);
        tf_stack_push(ctx, result);
        tf_obj_release(idx_obj);
        tf_obj_release(coll_obj);
        return TF_OK;
    }

    if (!index_in_bounds(ctx, idx, coll_obj->str.len)) return TF_ERR;
    idx_obj = tf_stack_pop(ctx);
    coll_obj = tf_stack_pop(ctx);
    char buf[2] = { coll_obj->str.ptr[(size_t)idx], 0 };
    tf_stack_push(ctx, tf_obj_new_string(buf, 1));
    tf_obj_release(idx_obj);
    tf_obj_release(coll_obj);
    return TF_OK;
}

tf_ret tf_set_at(tf_ctx *ctx) {
    if (!require_indexed(ctx, 2) ||
        !tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_INT)) {
        return TF_ERR;
    }
    tf_obj *val = tf_stack_peek(ctx, 0);
    tf_obj *idx_obj = tf_stack_peek(ctx, 1);
    tf_obj *coll_obj = tf_stack_peek(ctx, 2);

    int64_t idx = tf_obj_int_value(idx_obj);

    if (tf_obj_typeof(coll_obj) == TF_OBJ_TYPE_VECTOR) {
        if (!index_in_bounds(ctx, idx, coll_obj->vector.len)) return TF_ERR;
        val = tf_stack_pop(ctx);
        idx_obj = tf_stack_pop(ctx);
        coll_obj = tf_stack_pop(ctx);

        tf_obj *result = coll_obj->refcount == 1
                             ? coll_obj
                             : tf_vector_clone(coll_obj);
        tf_obj *old = result->vector.elem[(size_t)idx];
        result->vector.elem[(size_t)idx] = val;
        tf_obj_release(old);
        tf_stack_push(ctx, result);

        tf_obj_release(idx_obj);
        if (result != coll_obj) tf_obj_release(coll_obj);
        return TF_OK;
    }

    if (!index_in_bounds(ctx, idx, coll_obj->str.len)) return TF_ERR;
    if (!require_char(ctx, 0)) return TF_ERR;
    val = tf_stack_pop(ctx);
    idx_obj = tf_stack_pop(ctx);
    coll_obj = tf_stack_pop(ctx);

    tf_obj *result = coll_obj->refcount == 1
                         ? coll_obj
                         : tf_obj_new_string(coll_obj->str.ptr,
                                             coll_obj->str.len);
    result->str.ptr[(size_t)idx] = val->str.ptr[0];
    tf_stack_push(ctx, result);

    tf_obj_release(val);
    tf_obj_release(idx_obj);
    if (result != coll_obj) tf_obj_release(coll_obj);
    return TF_OK;
}

tf_ret tf_slice(tf_ctx *ctx) {
    if (!require_indexed(ctx, 2) ||
        !tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_INT) ||
        !tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_INT)) {
        return TF_ERR;
    }
    tf_obj *end_obj = tf_stack_peek(ctx, 0);
    tf_obj *start_obj = tf_stack_peek(ctx, 1);
    tf_obj *coll = tf_stack_peek(ctx, 2);

    end_obj = tf_stack_pop(ctx);
    start_obj = tf_stack_pop(ctx);
    coll = tf_stack_pop(ctx);

    int64_t start_arg = tf_obj_int_value(start_obj);
    int64_t end_arg = tf_obj_int_value(end_obj);
    tf_type coll_type = tf_obj_typeof(coll);
    size_t len =
        coll_type == TF_OBJ_TYPE_VECTOR ? coll->vector.len : coll->str.len;
    size_t start = start_arg <= 0 ? 0
                   : (uint64_t)start_arg > len ? len
                                               : (size_t)start_arg;
    size_t end = end_arg <= 0 ? 0
                 : (uint64_t)end_arg > len ? len
                                           : (size_t)end_arg;
    if (start > end) start = end;

    if (coll_type == TF_OBJ_TYPE_VECTOR) {
        tf_obj *res = tf_obj_new_vector_with_capacity((size_t)(end - start));
        for (size_t i = start; i < end; i++) {
            tf_obj_retain(coll->vector.elem[i]);
            tf_vector_push(res, coll->vector.elem[i]);
        }
        tf_stack_push(ctx, res);
    } else {
        tf_stack_push(ctx, tf_obj_new_string(coll->str.ptr + start,
                                             end - start));
    }

    tf_obj_release(start_obj);
    tf_obj_release(end_obj);
    tf_obj_release(coll);
    return TF_OK;
}

tf_ret tf_len(tf_ctx *ctx) {
    if (!require_countable(ctx, 0)) return TF_ERR;
    tf_obj *o = tf_stack_pop(ctx);
    tf_type type = tf_obj_typeof(o);
    size_t len = 0;
    if (type == TF_OBJ_TYPE_VECTOR) {
        len = o->vector.len;
    } else if (type == TF_OBJ_TYPE_LIST) {
        len = o->list.len;
    } else if (type == TF_OBJ_TYPE_STR) {
        len = o->str.len;
    } else if (type == TF_OBJ_TYPE_MAP) {
        len = o->map.len;
    } else if (type == TF_OBJ_TYPE_SET) {
        len = o->set.len;
    } else if (type == TF_OBJ_TYPE_DEQUE) {
        len = o->deque.len;
    } else {
        len = o->pqueue.len;
    }
    if (len > INT64_MAX) {
        tf_obj_release(o);
        tf_ctx_runtime_errorf(ctx, "'%s' collection length is not representable\n",
                              ctx->current_word);
        return TF_ERR;
    }
    tf_stack_push(ctx, tf_obj_new_int((int64_t)len));
    tf_obj_release(o);
    return TF_OK;
}

tf_ret tf_first(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *coll = tf_stack_peek(ctx, 0);
    tf_type type = tf_obj_typeof(coll);
    tf_obj *item = NULL;

    if (type == TF_OBJ_TYPE_VECTOR && coll->vector.len > 0) {
        item = coll->vector.elem[0];
    } else if (type == TF_OBJ_TYPE_LIST && coll->list.len > 0) {
        item = coll->list.head->value;
    } else if (type == TF_OBJ_TYPE_STR && coll->str.len > 0) {
        coll = tf_stack_pop(ctx);
        tf_stack_push(ctx, tf_obj_new_string(coll->str.ptr, 1));
        tf_obj_release(coll);
        return TF_OK;
    } else if (type == TF_OBJ_TYPE_DEQUE && coll->deque.len > 0) {
        item = tf_deque_get(coll, 0);
    } else if (type != TF_OBJ_TYPE_VECTOR && type != TF_OBJ_TYPE_LIST &&
               type != TF_OBJ_TYPE_STR && type != TF_OBJ_TYPE_DEQUE) {
        tf_ctx_runtime_errorf(
            ctx,
            "'%s' expected vector, list, string, or deque at stack depth 0, found %s\n",
            ctx->current_word, tf_obj_type_name(coll));
        return TF_ERR;
    } else {
        tf_ctx_runtime_errorf(ctx, "'%s' expected non-empty ordered collection\n",
                              ctx->current_word);
        return TF_ERR;
    }

    tf_obj_retain(item);
    coll = tf_stack_pop(ctx);
    tf_stack_push(ctx, item);
    tf_obj_release(coll);
    return TF_OK;
}

tf_ret tf_last(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *coll = tf_stack_peek(ctx, 0);
    tf_type type = tf_obj_typeof(coll);
    tf_obj *item = NULL;

    if (type == TF_OBJ_TYPE_VECTOR && coll->vector.len > 0) {
        item = coll->vector.elem[coll->vector.len - 1];
    } else if (type == TF_OBJ_TYPE_LIST && coll->list.len > 0) {
        item = tf_list_get(coll, coll->list.len - 1);
    } else if (type == TF_OBJ_TYPE_STR && coll->str.len > 0) {
        coll = tf_stack_pop(ctx);
        tf_stack_push(ctx,
                      tf_obj_new_string(coll->str.ptr + coll->str.len - 1, 1));
        tf_obj_release(coll);
        return TF_OK;
    } else if (type == TF_OBJ_TYPE_DEQUE && coll->deque.len > 0) {
        item = tf_deque_get(coll, coll->deque.len - 1);
    } else if (type != TF_OBJ_TYPE_VECTOR && type != TF_OBJ_TYPE_LIST &&
               type != TF_OBJ_TYPE_STR && type != TF_OBJ_TYPE_DEQUE) {
        tf_ctx_runtime_errorf(
            ctx,
            "'%s' expected vector, list, string, or deque at stack depth 0, found %s\n",
            ctx->current_word, tf_obj_type_name(coll));
        return TF_ERR;
    } else {
        tf_ctx_runtime_errorf(ctx, "'%s' expected non-empty ordered collection\n",
                              ctx->current_word);
        return TF_ERR;
    }

    tf_obj_retain(item);
    coll = tf_stack_pop(ctx);
    tf_stack_push(ctx, item);
    tf_obj_release(coll);
    return TF_OK;
}

tf_ret tf_rest(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 0)) return TF_ERR;
    tf_obj *seq = tf_stack_peek(ctx, 0);
    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_VECTOR) {
        seq = tf_stack_pop(ctx);
        size_t rest_len = seq->vector.len > 0 ? seq->vector.len - 1 : 0;
        tf_obj *rest = tf_obj_new_vector_with_capacity(rest_len);
        for (size_t i = 1; i < seq->vector.len; i++) {
            tf_obj *elem = seq->vector.elem[i];
            tf_obj_retain(elem);
            tf_vector_push(rest, elem);
        }
        tf_stack_push(ctx, rest);
        tf_obj_release(seq);
        return TF_OK;
    }
    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_LIST) {
        seq = tf_stack_pop(ctx);
        tf_stack_push(ctx, tf_list_rest_obj(seq));
        tf_obj_release(seq);
        return TF_OK;
    }

    seq = tf_stack_pop(ctx);
    size_t start = seq->str.len > 0 ? 1 : 0;
    tf_stack_push(ctx, tf_obj_new_string(seq->str.ptr + start,
                                      seq->str.len - start));
    tf_obj_release(seq);
    return TF_OK;
}

tf_ret tf_uncons(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 0)) return TF_ERR;
    tf_obj *seq = tf_stack_peek(ctx, 0);
    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_VECTOR && seq->vector.len > 0) {
        seq = tf_stack_pop(ctx);
        tf_obj *head = seq->vector.elem[0];
        tf_obj_retain(head);

        tf_obj *rest =
            tf_obj_new_vector_with_capacity(seq->vector.len - 1);
        for (size_t i = 1; i < seq->vector.len; i++) {
            tf_obj *elem = seq->vector.elem[i];
            tf_obj_retain(elem);
            tf_vector_push(rest, elem);
        }

        tf_stack_push(ctx, head);
        tf_stack_push(ctx, rest);
        tf_obj_release(seq);
        return TF_OK;
    } else if (tf_obj_typeof(seq) == TF_OBJ_TYPE_LIST && seq->list.len > 0) {
        seq = tf_stack_pop(ctx);
        tf_obj *head = seq->list.head->value;
        tf_obj_retain(head);
        tf_stack_push(ctx, head);
        tf_stack_push(ctx, tf_list_rest_obj(seq));
        tf_obj_release(seq);
        return TF_OK;
    } else if (tf_obj_typeof(seq) == TF_OBJ_TYPE_STR && seq->str.len > 0) {
        seq = tf_stack_pop(ctx);
        tf_stack_push(ctx, tf_obj_new_string(seq->str.ptr, 1));
        tf_stack_push(ctx, tf_obj_new_string(seq->str.ptr + 1, seq->str.len - 1));
        tf_obj_release(seq);
        return TF_OK;
    } else {
        tf_ctx_runtime_errorf(ctx, "'%s' expected non-empty sequence\n",
                              ctx->current_word);
        return TF_ERR;
    }
}

tf_ret tf_take(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 1) ||
        !tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_INT)) {
        return TF_ERR;
    }
    tf_obj *n_obj = tf_stack_peek(ctx, 0);
    tf_obj *coll = tf_stack_peek(ctx, 1);

    n_obj = tf_stack_pop(ctx);
    coll = tf_stack_pop(ctx);
    int64_t n = tf_obj_int_value(n_obj);
    tf_obj_release(n_obj);
    size_t len = sequence_len(coll);
    size_t count = n <= 0 ? 0
                   : (uint64_t)n > len ? len
                                       : (size_t)n;

    if (tf_obj_typeof(coll) == TF_OBJ_TYPE_VECTOR) {
        tf_obj *res = tf_obj_new_vector_with_capacity(count);
        for (size_t i = 0; i < count; i++) {
            tf_obj_retain(coll->vector.elem[i]);
            tf_vector_push(res, coll->vector.elem[i]);
        }
        tf_stack_push(ctx, res);
    } else if (tf_obj_typeof(coll) == TF_OBJ_TYPE_LIST) {
        tf_stack_push(ctx, tf_list_take_obj(coll, count));
    } else {
        tf_stack_push(ctx, tf_obj_new_string(coll->str.ptr, count));
    }
    tf_obj_release(coll);
    return TF_OK;
}

tf_ret tf_dropn(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 1) ||
        !tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_INT)) {
        return TF_ERR;
    }
    tf_obj *n_obj = tf_stack_peek(ctx, 0);
    tf_obj *coll = tf_stack_peek(ctx, 1);

    n_obj = tf_stack_pop(ctx);
    coll = tf_stack_pop(ctx);
    int64_t n = tf_obj_int_value(n_obj);
    tf_obj_release(n_obj);
    size_t len = sequence_len(coll);
    size_t count = n <= 0 ? 0
                   : (uint64_t)n > len ? len
                                       : (size_t)n;

    if (tf_obj_typeof(coll) == TF_OBJ_TYPE_VECTOR) {
        tf_obj *res = tf_obj_new_vector_with_capacity(coll->vector.len - count);
        for (size_t i = count; i < coll->vector.len; i++) {
            tf_obj_retain(coll->vector.elem[i]);
            tf_vector_push(res, coll->vector.elem[i]);
        }
        tf_stack_push(ctx, res);
    } else if (tf_obj_typeof(coll) == TF_OBJ_TYPE_LIST) {
        tf_stack_push(ctx, tf_list_drop_obj(coll, count));
    } else {
        tf_stack_push(ctx, tf_obj_new_string(coll->str.ptr + count,
                                             coll->str.len - count));
    }
    tf_obj_release(coll);
    return TF_OK;
}

tf_ret tf_cons(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 2)) return TF_ERR;
    tf_obj *seq = tf_stack_peek(ctx, 0);

    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_VECTOR) {
        seq = tf_stack_pop(ctx);
        tf_obj *head = tf_stack_pop(ctx);
        tf_obj *result =
            tf_obj_new_vector_with_capacity(seq->vector.len + 1);
        tf_vector_push(result, head);

        for (size_t i = 0; i < seq->vector.len; i++) {
            tf_obj *elem = seq->vector.elem[i];
            tf_obj_retain(elem);
            tf_vector_push(result, elem);
        }

        tf_stack_push(ctx, result);
        tf_obj_release(seq);
        return TF_OK;
    } else if (tf_obj_typeof(seq) == TF_OBJ_TYPE_LIST) {
        seq = tf_stack_pop(ctx);
        tf_obj *head = tf_stack_pop(ctx);
        tf_stack_push(ctx, tf_list_cons_obj(head, seq));
        tf_obj_release(head);
        tf_obj_release(seq);
        return TF_OK;
    } else if (tf_obj_typeof(seq) == TF_OBJ_TYPE_STR) {
        if (!require_char(ctx, 1)) return TF_ERR;

        seq = tf_stack_pop(ctx);
        tf_obj *head = tf_stack_pop(ctx);
        tf_obj *result = seq->refcount == 1
                             ? seq
                             : tf_obj_new_string_uninitialized(seq->str.len + 1);
        if (result == seq) {
            tf_string_reserve(result, result->str.len + 1);
            memmove(result->str.ptr + 1, result->str.ptr, result->str.len);
            result->str.len++;
            result->str.ptr[result->str.len] = '\0';
        } else {
            result->str.ptr[0] = head->str.ptr[0];
            memcpy(result->str.ptr + 1, seq->str.ptr, seq->str.len);
        }
        result->str.ptr[0] = head->str.ptr[0];
        tf_stack_push(ctx, result);
        tf_obj_release(head);
        if (result != seq) tf_obj_release(seq);
        return TF_OK;
    }

    tf_ctx_runtime_errorf(ctx, "'%s' expected sequence at stack depth 0, found %s\n",
                          ctx->current_word, tf_obj_type_name(seq));
    return TF_ERR;
}

tf_ret tf_concat(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 2)) return TF_ERR;
    tf_obj *right = tf_stack_peek(ctx, 0);
    tf_obj *left = tf_stack_peek(ctx, 1);
    tf_type left_type = tf_obj_typeof(left);
    tf_type right_type = tf_obj_typeof(right);

    if (left_type == TF_OBJ_TYPE_STR && right_type == TF_OBJ_TYPE_STR) {
        right = tf_stack_pop(ctx);
        left = tf_stack_pop(ctx);
        size_t new_len = left->str.len + right->str.len;
        size_t left_len = left->str.len;
        tf_obj *result = left->refcount == 1
                             ? left
                             : tf_obj_new_string_uninitialized(new_len);
        if (result == left) {
            tf_string_reserve(result, new_len);
        } else {
            memcpy(result->str.ptr, left->str.ptr, left_len);
        }
        memcpy(result->str.ptr + left_len, right->str.ptr, right->str.len);
        result->str.len = new_len;
        result->str.ptr[new_len] = '\0';
        tf_stack_push(ctx, result);
        if (result != left) tf_obj_release(left);
        tf_obj_release(right);
        return TF_OK;
    }

    if ((left_type != TF_OBJ_TYPE_VECTOR && left_type != TF_OBJ_TYPE_LIST) ||
        left_type != right_type) {
        tf_ctx_runtime_errorf(
            ctx, "'%s' expected matching vector, list, or string values, found %s and %s\n",
            ctx->current_word, tf_obj_type_name(left), tf_obj_type_name(right));
        return TF_ERR;
    }

    right = tf_stack_pop(ctx);
    left = tf_stack_pop(ctx);

    if (left_type == TF_OBJ_TYPE_LIST) {
        tf_stack_push(ctx, tf_list_concat_obj(left, right));
        tf_obj_release(left);
        tf_obj_release(right);
        return TF_OK;
    }

    size_t left_len = sequence_len(left);
    size_t right_len = sequence_len(right);
    size_t result_len = left_len + right_len;

    if (left_type == TF_OBJ_TYPE_VECTOR && left->refcount == 1) {
        tf_vector_reserve(left, result_len);
        push_sequence_items_to_vector(left, right, 0, right_len);
        tf_stack_push(ctx, left);
    } else {
        tf_obj *result = tf_obj_new_vector_with_capacity(result_len);
        push_sequence_items_to_vector(result, left, 0, left_len);
        push_sequence_items_to_vector(result, right, 0, right_len);
        tf_obj *out = vector_to_sequence_family(result, left);
        tf_stack_push(ctx, out);
        if (out != result) tf_obj_release(result);
        tf_obj_release(left);
    }
    tf_obj_release(right);
    return TF_OK;
}

tf_ret tf_reverse(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 0)) return TF_ERR;
    tf_obj *seq = tf_stack_pop(ctx);

    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_VECTOR) {
        tf_obj *result = tf_obj_new_vector_with_capacity(seq->vector.len);
        for (size_t i = seq->vector.len; i > 0; i--) {
            tf_obj *elem = seq->vector.elem[i - 1];
            tf_obj_retain(elem);
            tf_vector_push(result, elem);
        }
        tf_stack_push(ctx, result);
        tf_obj_release(seq);
        return TF_OK;
    }
    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_LIST) {
        tf_stack_push(ctx, tf_list_reverse_obj(seq));
        tf_obj_release(seq);
        return TF_OK;
    }

    tf_obj *result = tf_obj_new_string_uninitialized(seq->str.len);
    for (size_t i = 0; i < seq->str.len; i++) {
        result->str.ptr[i] = seq->str.ptr[seq->str.len - 1 - i];
    }
    tf_stack_push(ctx, result);
    tf_obj_release(seq);
    return TF_OK;
}

tf_ret tf_split_string(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_STR) ||
        !tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_STR)) {
        return TF_ERR;
    }
    tf_obj *arg2 = tf_stack_pop(ctx);
    tf_obj *arg1 = tf_stack_pop(ctx);

    tf_obj *result = tf_obj_new_vector();
    char *start = arg1->str.ptr;
    char *sep = arg2->str.ptr;
    size_t sep_len = arg2->str.len;
    size_t remaining = arg1->str.len;

    if (sep_len == 0) {
        for (size_t i = 0; i < arg1->str.len; i++) {
            tf_vector_push(result, tf_obj_new_string(arg1->str.ptr + i, 1));
        }
    } else {
        char *p;
        while ((p = find_mem(start, remaining, sep, sep_len)) != NULL) {
            tf_vector_push(result, tf_obj_new_string(start, p - start));
            size_t consumed = (size_t)(p - start) + sep_len;
            start = p + sep_len;
            remaining -= consumed;
        }
        tf_vector_push(result, tf_obj_new_string(start, remaining));
    }

    tf_stack_push(ctx, result);
    tf_obj_release(arg1);
    tf_obj_release(arg2);
    return TF_OK;
}

tf_ret tf_to_vector(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 0)) return TF_ERR;
    tf_obj *seq = tf_stack_pop(ctx);
    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_VECTOR) {
        tf_stack_push(ctx, seq);
        return TF_OK;
    }
    tf_stack_push(ctx, sequence_to_vector(seq));
    tf_obj_release(seq);
    return TF_OK;
}

tf_ret tf_to_list(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 0)) return TF_ERR;
    tf_obj *seq = tf_stack_pop(ctx);
    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_LIST) {
        tf_stack_push(ctx, seq);
        return TF_OK;
    }

    tf_list_builder builder;
    tf_list_builder_init(&builder);
    tf_sequence_iter iter;
    tf_sequence_iter_init(&iter, seq);
    while (true) {
        tf_obj *item = tf_sequence_iter_next_owned(&iter);
        if (!item) break;
        tf_list_builder_push_owned(&builder, item);
    }
    tf_stack_push(ctx, tf_list_builder_finish(&builder));
    tf_obj_release(seq);
    return TF_OK;
}

tf_ret tf_to_string(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 0)) return TF_ERR;
    tf_obj *seq = tf_stack_peek(ctx, 0);
    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_STR) {
        seq = tf_stack_pop(ctx);
        tf_stack_push(ctx, seq);
        return TF_OK;
    }

    tf_sequence_iter iter;
    tf_sequence_iter_init(&iter, seq);
    for (size_t i = 0;; i++) {
        tf_obj *item = tf_sequence_iter_next_owned(&iter);
        if (!item) break;
        bool valid = is_char_obj(item);
        tf_obj_release(item);
        if (!valid) {
            tf_ctx_runtime_errorf(
                ctx,
                "'%s' expected item %zu to be a one-character string\n",
                ctx->current_word, i);
            return TF_ERR;
        }
    }

    seq = tf_stack_pop(ctx);
    tf_obj *result = tf_obj_new_string_uninitialized(sequence_len(seq));
    tf_sequence_iter_init(&iter, seq);
    for (size_t i = 0;; i++) {
        tf_obj *item = tf_sequence_iter_next_owned(&iter);
        if (!item) break;
        result->str.ptr[i] = item->str.ptr[0];
        tf_obj_release(item);
    }
    tf_stack_push(ctx, result);
    tf_obj_release(seq);
    return TF_OK;
}

tf_ret tf_to_map(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *pairs = tf_stack_peek(ctx, 0);
    tf_type type = tf_obj_typeof(pairs);
    if (type != TF_OBJ_TYPE_VECTOR && type != TF_OBJ_TYPE_LIST) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected vector or list of pairs, found %s\n",
                              ctx->current_word, tf_obj_type_name(pairs));
        return TF_ERR;
    }
    pairs = tf_stack_pop(ctx);
    tf_obj *map = tf_obj_new_map();
    tf_map_reserve(map, sequence_len(pairs));

    tf_sequence_iter iter;
    tf_sequence_iter_init(&iter, pairs);
    for (size_t i = 0;; i++) {
        tf_obj *pair = tf_sequence_iter_next_owned(&iter);
        if (!pair) break;
        tf_obj *key = NULL;
        tf_obj *value = NULL;
        if (!pair_items(pair, &key, &value)) {
            report_pair_error(ctx, i, pair);
            tf_obj_release(pair);
            tf_obj_release(map);
            tf_obj_release(pairs);
            return TF_ERR;
        }

        if (!tf_obj_hashable(key)) {
            tf_ctx_runtime_errorf(
                ctx, "'%s' expected hashable key at item %zu, found %s\n",
                ctx->current_word, i, tf_obj_type_name(key));
            tf_obj_release(pair);
            tf_obj_release(map);
            tf_obj_release(pairs);
            return TF_ERR;
        }
        if (tf_map_has(map, key)) {
            tf_ctx_runtime_errorf(ctx, "'%s' duplicate map key at item %zu\n",
                                  ctx->current_word, i);
            tf_obj_release(pair);
            tf_obj_release(map);
            tf_obj_release(pairs);
            return TF_ERR;
        }
        tf_map_set(map, key, value);
        tf_obj_release(pair);
    }

    tf_stack_push(ctx, map);
    tf_obj_release(pairs);
    return TF_OK;
}

tf_ret tf_to_set(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 0)) return TF_ERR;
    tf_obj *seq = tf_stack_pop(ctx);
    tf_obj *set = tf_obj_new_set();

    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_VECTOR) {
        for (size_t i = 0; i < seq->vector.len; i++) {
            tf_obj *item = seq->vector.elem[i];
            if (!tf_obj_hashable(item)) {
                tf_ctx_runtime_errorf(
                    ctx, "'%s' expected hashable set item at index %zu, found %s\n",
                    ctx->current_word, i, tf_obj_type_name(item));
                tf_obj_release(set);
                tf_obj_release(seq);
                return TF_ERR;
            }
            tf_set_add(set, item);
        }
    } else if (tf_obj_typeof(seq) == TF_OBJ_TYPE_LIST) {
        size_t idx = 0;
        for (tf_list_node *node = seq->list.head; node; node = node->next, idx++) {
            tf_obj *item = node->value;
            if (!tf_obj_hashable(item)) {
                tf_ctx_runtime_errorf(
                    ctx, "'%s' expected hashable set item at index %zu, found %s\n",
                    ctx->current_word, idx, tf_obj_type_name(item));
                tf_obj_release(set);
                tf_obj_release(seq);
                return TF_ERR;
            }
            tf_set_add(set, item);
        }
    } else {
        for (size_t i = 0; i < seq->str.len; i++) {
            tf_obj *item = tf_obj_new_string(seq->str.ptr + i, 1);
            tf_set_add(set, item);
            tf_obj_release(item);
        }
    }

    tf_stack_push(ctx, set);
    tf_obj_release(seq);
    return TF_OK;
}

tf_ret tf_to_deque(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 0)) return TF_ERR;
    tf_obj *seq = tf_stack_pop(ctx);
    tf_obj *deque = tf_obj_new_deque();
    tf_deque_reserve(deque, sequence_len(seq));

    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_VECTOR) {
        for (size_t i = 0; i < seq->vector.len; i++) {
            tf_deque_push_back(deque, seq->vector.elem[i]);
        }
    } else if (tf_obj_typeof(seq) == TF_OBJ_TYPE_LIST) {
        for (tf_list_node *node = seq->list.head; node; node = node->next) {
            tf_deque_push_back(deque, node->value);
        }
    } else {
        for (size_t i = 0; i < seq->str.len; i++) {
            tf_obj *item = tf_obj_new_string(seq->str.ptr + i, 1);
            tf_deque_push_back(deque, item);
            tf_obj_release(item);
        }
    }

    tf_stack_push(ctx, deque);
    tf_obj_release(seq);
    return TF_OK;
}

tf_ret tf_to_pqueue(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *pairs = tf_stack_peek(ctx, 0);
    tf_type type = tf_obj_typeof(pairs);
    if (type != TF_OBJ_TYPE_VECTOR && type != TF_OBJ_TYPE_LIST) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected vector or list of pairs, found %s\n",
                              ctx->current_word, tf_obj_type_name(pairs));
        return TF_ERR;
    }
    pairs = tf_stack_pop(ctx);
    tf_obj *pqueue = tf_obj_new_pqueue();
    tf_pqueue_reserve(pqueue, sequence_len(pairs));

    tf_sequence_iter iter;
    tf_sequence_iter_init(&iter, pairs);
    for (size_t i = 0;; i++) {
        tf_obj *pair = tf_sequence_iter_next_owned(&iter);
        if (!pair) break;
        tf_obj *priority_obj = NULL;
        tf_obj *value = NULL;
        if (!pair_items(pair, &priority_obj, &value)) {
            report_pair_error(ctx, i, pair);
            tf_obj_release(pair);
            tf_obj_release(pqueue);
            tf_obj_release(pairs);
            return TF_ERR;
        }

        double priority = 0;
        if (tf_obj_typeof(priority_obj) == TF_OBJ_TYPE_INT) {
            priority = (double)tf_obj_int_value(priority_obj);
        } else if (tf_obj_typeof(priority_obj) == TF_OBJ_TYPE_FLOAT) {
            priority = priority_obj->f;
        } else {
            tf_ctx_runtime_errorf(
                ctx, "'%s' expected numeric priority at item %zu, found %s\n",
                ctx->current_word, i, tf_obj_type_name(priority_obj));
            tf_obj_release(pair);
            tf_obj_release(pqueue);
            tf_obj_release(pairs);
            return TF_ERR;
        }
        if (!isfinite(priority)) {
            tf_ctx_runtime_errorf(ctx, "'%s' expected finite priority at item %zu\n",
                                  ctx->current_word, i);
            tf_obj_release(pair);
            tf_obj_release(pqueue);
            tf_obj_release(pairs);
            return TF_ERR;
        }
        tf_pqueue_append(pqueue, priority_obj, value);
        tf_obj_release(pair);
    }

    tf_pqueue_heapify(pqueue);

    tf_stack_push(ctx, pqueue);
    tf_obj_release(pairs);
    return TF_OK;
}

tf_ret tf_contains_q(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 2) || !tf_ctx_require_sequence(ctx, 1)) {
        return TF_ERR;
    }

    tf_obj *seq_arg = tf_stack_peek(ctx, 1);
    tf_obj *item_arg = tf_stack_peek(ctx, 0);
    if (tf_obj_typeof(seq_arg) == TF_OBJ_TYPE_STR &&
        tf_obj_typeof(item_arg) != TF_OBJ_TYPE_STR) {
        tf_ctx_runtime_errorf(ctx,
                              "'%s' expected a string substring, found %s\n",
                              ctx->current_word, tf_obj_type_name(item_arg));
        return TF_ERR;
    }

    tf_obj *item = tf_stack_pop(ctx);
    tf_obj *seq = tf_stack_pop(ctx);
    tf_stack_push(ctx, tf_obj_new_bool(sequence_indexof(seq, item) >= 0));
    tf_obj_release(item);
    tf_obj_release(seq);
    return TF_OK;
}

tf_ret tf_indexof(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 2) || !tf_ctx_require_sequence(ctx, 1)) {
        return TF_ERR;
    }

    tf_obj *seq_arg = tf_stack_peek(ctx, 1);
    tf_obj *item_arg = tf_stack_peek(ctx, 0);
    if (tf_obj_typeof(seq_arg) == TF_OBJ_TYPE_STR &&
        tf_obj_typeof(item_arg) != TF_OBJ_TYPE_STR) {
        tf_ctx_runtime_errorf(ctx,
                              "'%s' expected a string substring, found %s\n",
                              ctx->current_word, tf_obj_type_name(item_arg));
        return TF_ERR;
    }

    tf_obj *item = tf_stack_pop(ctx);
    tf_obj *seq = tf_stack_pop(ctx);
    tf_stack_push(ctx, tf_obj_new_int(sequence_indexof(seq, item)));
    tf_obj_release(item);
    tf_obj_release(seq);
    return TF_OK;
}

tf_ret tf_unique(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 0)) return TF_ERR;
    tf_obj *seq = tf_stack_pop(ctx);

    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_STR) {
        bool seen[256] = { false };
        char *buf = tf_xmalloc(seq->str.len + 1);
        size_t len = 0;
        for (size_t i = 0; i < seq->str.len; i++) {
            unsigned char c = (unsigned char)seq->str.ptr[i];
            if (seen[c]) continue;
            seen[c] = true;
            buf[len++] = (char)c;
        }
        buf[len] = '\0';
        tf_stack_push(ctx, tf_obj_new_string_take(buf, len));
        tf_obj_release(seq);
        return TF_OK;
    }

    tf_obj *items = tf_obj_new_vector();
    tf_obj *seen_hashable = NULL;
    size_t hashable_count = 0;
    tf_sequence_iter iter;
    tf_sequence_iter_init(&iter, seq);
    while (true) {
        tf_obj *item = tf_sequence_iter_next_owned(&iter);
        if (!item) break;

        bool hashable = tf_obj_hashable(item);
        if (hashable && !seen_hashable &&
            hashable_count >= TF_UNIQUE_HASH_CUTOFF) {
            seen_hashable = tf_obj_new_set();
            tf_set_reserve(seen_hashable, hashable_count);
            for (size_t i = 0; i < items->vector.len; i++) {
                tf_obj *seen = items->vector.elem[i];
                if (tf_obj_hashable(seen)) tf_set_add(seen_hashable, seen);
            }
        }

        bool duplicate = seen_hashable && hashable
                             ? tf_set_has(seen_hashable, item)
                             : vector_contains_equal(items, item);
        if (duplicate) {
            tf_obj_release(item);
            continue;
        }
        if (seen_hashable && hashable) tf_set_add(seen_hashable, item);
        tf_vector_push(items, item);
        if (hashable) hashable_count++;
    }
    if (seen_hashable) tf_obj_release(seen_hashable);

    tf_obj *out = vector_to_sequence_family(items, seq);
    tf_stack_push(ctx, out);
    if (out != items) tf_obj_release(items);
    tf_obj_release(seq);
    return TF_OK;
}

tf_ret tf_sort(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 0)) return TF_ERR;
    tf_obj *seq = tf_stack_pop(ctx);

    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_STR) {
        tf_obj *result = tf_obj_new_string_uninitialized(seq->str.len);
        memcpy(result->str.ptr, seq->str.ptr, seq->str.len);
        sort_string_bytes(result->str.ptr, result->str.len);
        tf_stack_push(ctx, result);
        tf_obj_release(seq);
        return TF_OK;
    }

    tf_obj *items = sequence_to_vector(seq);
    if (!sort_vector_natural(ctx, items)) {
        tf_obj_release(items);
        tf_obj_release(seq);
        return TF_ERR;
    }

    tf_obj *out = vector_to_sequence_family(items, seq);
    tf_stack_push(ctx, out);
    if (out != items) tf_obj_release(items);
    tf_obj_release(seq);
    return TF_OK;
}

tf_ret tf_has_q(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 2)) return TF_ERR;
    tf_obj *coll = tf_stack_peek(ctx, 1);
    tf_obj *key = tf_stack_peek(ctx, 0);
    tf_type coll_type = tf_obj_typeof(coll);

    if (coll_type != TF_OBJ_TYPE_MAP && coll_type != TF_OBJ_TYPE_SET) {
        tf_ctx_runtime_errorf(ctx,
                              "'%s' expected map or set at stack depth 1, found %s\n",
                              ctx->current_word, tf_obj_type_name(coll));
        return TF_ERR;
    }
    if (!tf_obj_hashable(key)) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected hashable key/item, found %s\n",
                              ctx->current_word, tf_obj_type_name(key));
        return TF_ERR;
    }

    key = tf_stack_pop(ctx);
    coll = tf_stack_pop(ctx);
    bool result = coll_type == TF_OBJ_TYPE_MAP ? tf_map_has(coll, key)
                                               : tf_set_has(coll, key);
    tf_stack_push(ctx, tf_obj_new_bool(result));
    tf_obj_release(key);
    tf_obj_release(coll);
    return TF_OK;
}

tf_ret tf_get(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_MAP) ||
        !tf_ctx_require_stack(ctx, 2)) {
        return TF_ERR;
    }
    tf_obj *map = tf_stack_peek(ctx, 1);
    tf_obj *key = tf_stack_peek(ctx, 0);
    if (!tf_obj_hashable(key)) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected hashable key, found %s\n",
                              ctx->current_word, tf_obj_type_name(key));
        return TF_ERR;
    }

    tf_obj *value = NULL;
    if (!tf_map_get(map, key, &value)) {
        tf_ctx_runtime_errorf(ctx, "'%s' key not found\n", ctx->current_word);
        return TF_ERR;
    }

    key = tf_stack_pop(ctx);
    map = tf_stack_pop(ctx);
    tf_stack_push(ctx, value);
    tf_obj_retain(value);
    tf_obj_release(key);
    tf_obj_release(map);
    return TF_OK;
}

tf_ret tf_get_or(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 2, TF_OBJ_TYPE_MAP) ||
        !tf_ctx_require_stack(ctx, 3)) {
        return TF_ERR;
    }
    tf_obj *map = tf_stack_peek(ctx, 2);
    tf_obj *key = tf_stack_peek(ctx, 1);
    if (!tf_obj_hashable(key)) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected hashable key, found %s\n",
                              ctx->current_word, tf_obj_type_name(key));
        return TF_ERR;
    }

    tf_obj *value = NULL;
    bool found = tf_map_get(map, key, &value);
    tf_obj *default_value = tf_stack_pop(ctx);
    key = tf_stack_pop(ctx);
    map = tf_stack_pop(ctx);

    if (found) {
        tf_obj_retain(value);
        tf_stack_push(ctx, value);
        tf_obj_release(default_value);
    } else {
        tf_stack_push(ctx, default_value);
    }
    tf_obj_release(key);
    tf_obj_release(map);
    return TF_OK;
}

tf_ret tf_assoc(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 2, TF_OBJ_TYPE_MAP) ||
        !tf_ctx_require_stack(ctx, 3)) {
        return TF_ERR;
    }
    tf_obj *map = tf_stack_peek(ctx, 2);
    tf_obj *key = tf_stack_peek(ctx, 1);
    if (!tf_obj_hashable(key)) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected hashable key, found %s\n",
                              ctx->current_word, tf_obj_type_name(key));
        return TF_ERR;
    }

    tf_obj *value = tf_stack_pop(ctx);
    key = tf_stack_pop(ctx);
    map = tf_stack_pop(ctx);

    tf_obj *result = map->refcount == 1 ? map : tf_map_clone(map);
    tf_map_set(result, key, value);
    tf_stack_push(ctx, result);

    tf_obj_release(value);
    tf_obj_release(key);
    if (result != map) tf_obj_release(map);
    return TF_OK;
}

tf_ret tf_dissoc(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_MAP) ||
        !tf_ctx_require_stack(ctx, 2)) {
        return TF_ERR;
    }
    tf_obj *map = tf_stack_peek(ctx, 1);
    tf_obj *key = tf_stack_peek(ctx, 0);
    if (!tf_obj_hashable(key)) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected hashable key, found %s\n",
                              ctx->current_word, tf_obj_type_name(key));
        return TF_ERR;
    }

    bool found = tf_map_has(map, key);
    key = tf_stack_pop(ctx);
    map = tf_stack_pop(ctx);
    if (!found) {
        tf_stack_push(ctx, map);
        tf_obj_release(key);
        return TF_OK;
    }

    tf_obj *result = map->refcount == 1 ? map : tf_map_clone(map);
    tf_map_remove(result, key);
    tf_stack_push(ctx, result);
    tf_obj_release(key);
    if (result != map) tf_obj_release(map);
    return TF_OK;
}

tf_ret tf_keys(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_MAP)) return TF_ERR;
    tf_obj *map = tf_stack_pop_type(ctx, TF_OBJ_TYPE_MAP);
    tf_obj *keys = tf_obj_new_vector_with_capacity(map->map.len);
    for (size_t i = 0; i < map->map.len; i++) {
        tf_obj *key = map->map.entries[i].key;
        tf_obj_retain(key);
        tf_vector_push(keys, key);
    }
    tf_stack_push(ctx, keys);
    tf_obj_release(map);
    return TF_OK;
}

tf_ret tf_values(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_MAP)) return TF_ERR;
    tf_obj *map = tf_stack_pop_type(ctx, TF_OBJ_TYPE_MAP);
    tf_obj *values = tf_obj_new_vector_with_capacity(map->map.len);
    for (size_t i = 0; i < map->map.len; i++) {
        tf_obj *value = map->map.entries[i].value;
        tf_obj_retain(value);
        tf_vector_push(values, value);
    }
    tf_stack_push(ctx, values);
    tf_obj_release(map);
    return TF_OK;
}

tf_ret tf_pairs(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *coll = tf_stack_peek(ctx, 0);
    tf_type type = tf_obj_typeof(coll);
    if (type != TF_OBJ_TYPE_MAP && type != TF_OBJ_TYPE_PQUEUE) {
        tf_ctx_runtime_errorf(ctx,
                              "'%s' expected map or pqueue, found %s\n",
                              ctx->current_word, tf_obj_type_name(coll));
        return TF_ERR;
    }

    coll = tf_stack_pop(ctx);
    tf_obj *pairs = NULL;
    if (type == TF_OBJ_TYPE_MAP) {
        pairs = tf_obj_new_vector_with_capacity(coll->map.len);
        for (size_t i = 0; i < coll->map.len; i++) {
            tf_obj *pair = tf_obj_new_vector();
            tf_obj *key = coll->map.entries[i].key;
            tf_obj *value = coll->map.entries[i].value;
            tf_obj_retain(key);
            tf_obj_retain(value);
            tf_vector_push(pair, key);
            tf_vector_push(pair, value);
            tf_vector_push(pairs, pair);
        }
        tf_stack_push(ctx, pairs);
        tf_obj_release(coll);
        return TF_OK;
    }

    tf_obj *tmp = coll->refcount == 1 ? coll : tf_pqueue_clone(coll);
    bool cloned = tmp != coll;
    pairs = tf_obj_new_vector_with_capacity(coll->pqueue.len);
    while (tmp->pqueue.len > 0) {
        tf_obj *priority = NULL;
        tf_obj *value = NULL;
        tf_pqueue_pop(tmp, &priority, &value);
        tf_obj *pair = tf_obj_new_vector();
        tf_vector_push(pair, priority);
        tf_vector_push(pair, value);
        tf_vector_push(pairs, pair);
    }
    tf_stack_push(ctx, pairs);
    tf_obj_release(tmp);
    if (cloned) tf_obj_release(coll);
    return TF_OK;
}

tf_ret tf_items(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *coll = tf_stack_peek(ctx, 0);
    tf_type type = tf_obj_typeof(coll);
    if (type != TF_OBJ_TYPE_SET && type != TF_OBJ_TYPE_DEQUE) {
        tf_ctx_runtime_errorf(ctx,
                              "'%s' expected set or deque, found %s\n",
                              ctx->current_word, tf_obj_type_name(coll));
        return TF_ERR;
    }

    coll = tf_stack_pop(ctx);
    size_t len = type == TF_OBJ_TYPE_SET ? coll->set.len : coll->deque.len;
    tf_obj *items = tf_obj_new_vector_with_capacity(len);
    if (type == TF_OBJ_TYPE_SET) {
        for (size_t i = 0; i < coll->set.len; i++) {
            tf_obj *item = coll->set.entries[i].item;
            tf_obj_retain(item);
            tf_vector_push(items, item);
        }
    } else {
        for (size_t i = 0; i < coll->deque.len; i++) {
            tf_obj *item = tf_deque_get(coll, i);
            tf_obj_retain(item);
            tf_vector_push(items, item);
        }
    }
    tf_stack_push(ctx, items);
    tf_obj_release(coll);
    return TF_OK;
}

tf_ret tf_insert(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_SET) ||
        !tf_ctx_require_stack(ctx, 2)) {
        return TF_ERR;
    }
    tf_obj *set = tf_stack_peek(ctx, 1);
    tf_obj *item = tf_stack_peek(ctx, 0);
    if (!tf_obj_hashable(item)) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected hashable set item, found %s\n",
                              ctx->current_word, tf_obj_type_name(item));
        return TF_ERR;
    }

    bool present = tf_set_has(set, item);
    item = tf_stack_pop(ctx);
    set = tf_stack_pop(ctx);
    if (present) {
        tf_stack_push(ctx, set);
        tf_obj_release(item);
        return TF_OK;
    }

    tf_obj *result = set->refcount == 1 ? set : tf_set_clone(set);
    tf_set_add(result, item);
    tf_stack_push(ctx, result);
    tf_obj_release(item);
    if (result != set) tf_obj_release(set);
    return TF_OK;
}

tf_ret tf_remove(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_SET) ||
        !tf_ctx_require_stack(ctx, 2)) {
        return TF_ERR;
    }
    tf_obj *set = tf_stack_peek(ctx, 1);
    tf_obj *item = tf_stack_peek(ctx, 0);
    if (!tf_obj_hashable(item)) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected hashable set item, found %s\n",
                              ctx->current_word, tf_obj_type_name(item));
        return TF_ERR;
    }

    bool present = tf_set_has(set, item);
    item = tf_stack_pop(ctx);
    set = tf_stack_pop(ctx);
    if (!present) {
        tf_stack_push(ctx, set);
        tf_obj_release(item);
        return TF_OK;
    }

    tf_obj *result = set->refcount == 1 ? set : tf_set_clone(set);
    tf_set_remove(result, item);
    tf_stack_push(ctx, result);
    tf_obj_release(item);
    if (result != set) tf_obj_release(set);
    return TF_OK;
}

tf_ret tf_union(tf_ctx *ctx) {
    if (!require_set_pair(ctx)) return TF_ERR;
    tf_obj *left = tf_stack_peek(ctx, 1);
    tf_obj *right = tf_stack_peek(ctx, 0);
    bool changes = left->refcount == 1 || !set_is_subset(right, left);

    right = tf_stack_pop(ctx);
    left = tf_stack_pop(ctx);
    if (!changes) {
        tf_stack_push(ctx, left);
        tf_obj_release(right);
        return TF_OK;
    }

    tf_obj *result = left->refcount == 1 ? left : tf_set_clone(left);
    for (size_t i = 0; i < right->set.len; i++) {
        tf_set_add(result, right->set.entries[i].item);
    }
    tf_stack_push(ctx, result);
    tf_obj_release(right);
    if (result != left) tf_obj_release(left);
    return TF_OK;
}

tf_ret tf_intersection(tf_ctx *ctx) {
    if (!require_set_pair(ctx)) return TF_ERR;
    tf_obj *right = tf_stack_pop(ctx);
    tf_obj *left = tf_stack_pop(ctx);
    tf_obj *result = NULL;
    if (set_is_subset(left, right)) {
        result = left;
    } else {
        result = set_select(left, right, true);
    }
    tf_stack_push(ctx, result);
    tf_obj_release(right);
    if (result != left) tf_obj_release(left);
    return TF_OK;
}

tf_ret tf_difference(tf_ctx *ctx) {
    if (!require_set_pair(ctx)) return TF_ERR;
    tf_obj *right = tf_stack_pop(ctx);
    tf_obj *left = tf_stack_pop(ctx);
    tf_obj *result = NULL;
    if (sets_are_disjoint(left, right)) {
        result = left;
    } else {
        result = set_select(left, right, false);
    }
    tf_stack_push(ctx, result);
    tf_obj_release(right);
    if (result != left) tf_obj_release(left);
    return TF_OK;
}

tf_ret tf_symmetric_difference(tf_ctx *ctx) {
    if (!require_set_pair(ctx)) return TF_ERR;
    tf_obj *right = tf_stack_pop(ctx);
    tf_obj *left = tf_stack_pop(ctx);
    if (left->set.len == 0 || right->set.len == 0) {
        tf_obj *result = left->set.len == 0 ? right : left;
        tf_obj *discard = result == left ? right : left;
        tf_stack_push(ctx, result);
        tf_obj_release(discard);
        return TF_OK;
    }

    tf_obj *result = set_select(left, right, false);
    for (size_t i = 0; i < right->set.len; i++) {
        tf_obj *item = right->set.entries[i].item;
        if (!tf_set_has(left, item)) tf_set_add(result, item);
    }
    tf_stack_push(ctx, result);
    tf_obj_release(left);
    tf_obj_release(right);
    return TF_OK;
}

static tf_ret set_relation_result(tf_ctx *ctx, bool result) {
    tf_obj *right = tf_stack_pop(ctx);
    tf_obj *left = tf_stack_pop(ctx);
    tf_stack_push(ctx, tf_obj_new_bool(result));
    tf_obj_release(left);
    tf_obj_release(right);
    return TF_OK;
}

tf_ret tf_subset_q(tf_ctx *ctx) {
    if (!require_set_pair(ctx)) return TF_ERR;
    return set_relation_result(
        ctx, set_is_subset(tf_stack_peek(ctx, 1), tf_stack_peek(ctx, 0)));
}

tf_ret tf_proper_subset_q(tf_ctx *ctx) {
    if (!require_set_pair(ctx)) return TF_ERR;
    tf_obj *left = tf_stack_peek(ctx, 1);
    tf_obj *right = tf_stack_peek(ctx, 0);
    return set_relation_result(
        ctx, left->set.len < right->set.len && set_is_subset(left, right));
}

tf_ret tf_superset_q(tf_ctx *ctx) {
    if (!require_set_pair(ctx)) return TF_ERR;
    return set_relation_result(
        ctx, set_is_subset(tf_stack_peek(ctx, 0), tf_stack_peek(ctx, 1)));
}

tf_ret tf_proper_superset_q(tf_ctx *ctx) {
    if (!require_set_pair(ctx)) return TF_ERR;
    tf_obj *left = tf_stack_peek(ctx, 1);
    tf_obj *right = tf_stack_peek(ctx, 0);
    return set_relation_result(
        ctx, left->set.len > right->set.len && set_is_subset(right, left));
}

tf_ret tf_disjoint_q(tf_ctx *ctx) {
    if (!require_set_pair(ctx)) return TF_ERR;
    return set_relation_result(
        ctx, sets_are_disjoint(tf_stack_peek(ctx, 1), tf_stack_peek(ctx, 0)));
}

tf_ret tf_push_front(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_DEQUE) ||
        !tf_ctx_require_stack(ctx, 2)) {
        return TF_ERR;
    }

    tf_obj *item = tf_stack_pop(ctx);
    tf_obj *deque = tf_stack_pop_type(ctx, TF_OBJ_TYPE_DEQUE);
    tf_obj *result = deque->refcount == 1 ? deque : tf_deque_clone(deque);
    tf_deque_push_front(result, item);
    tf_stack_push(ctx, result);
    tf_obj_release(item);
    if (result != deque) tf_obj_release(deque);
    return TF_OK;
}

tf_ret tf_push_back(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 2)) return TF_ERR;
    tf_obj *seq = tf_stack_peek(ctx, 1);
    tf_obj *item = tf_stack_peek(ctx, 0);

    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_VECTOR) {
        item = tf_stack_pop(ctx);
        seq = tf_stack_pop(ctx);
        tf_obj *result = seq->refcount == 1 ? seq : tf_vector_clone(seq);
        tf_vector_push(result, item);
        tf_stack_push(ctx, result);
        if (result != seq) tf_obj_release(seq);
        return TF_OK;
    }

    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_LIST) {
        item = tf_stack_pop(ctx);
        seq = tf_stack_pop(ctx);
        tf_stack_push(ctx, tf_list_push_back_obj(seq, item));
        tf_obj_release(item);
        tf_obj_release(seq);
        return TF_OK;
    }

    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_STR) {
        if (!require_char(ctx, 0)) return TF_ERR;
        item = tf_stack_pop(ctx);
        seq = tf_stack_pop(ctx);
        size_t old_len = seq->str.len;
        tf_obj *result = seq->refcount == 1
                             ? seq
                             : tf_obj_new_string_uninitialized(old_len + 1);
        if (result == seq) {
            tf_string_reserve(result, old_len + 1);
        } else {
            memcpy(result->str.ptr, seq->str.ptr, old_len);
        }
        result->str.ptr[old_len] = item->str.ptr[0];
        result->str.len = old_len + 1;
        result->str.ptr[result->str.len] = '\0';
        tf_stack_push(ctx, result);
        tf_obj_release(item);
        if (result != seq) tf_obj_release(seq);
        return TF_OK;
    }

    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_DEQUE) {
        item = tf_stack_pop(ctx);
        seq = tf_stack_pop(ctx);
        tf_obj *result = seq->refcount == 1 ? seq : tf_deque_clone(seq);
        tf_deque_push_back(result, item);
        tf_stack_push(ctx, result);
        tf_obj_release(item);
        if (result != seq) tf_obj_release(seq);
        return TF_OK;
    }

    tf_ctx_runtime_errorf(
        ctx,
        "'%s' expected vector, list, string, or deque at stack depth 1, found %s\n",
        ctx->current_word, tf_obj_type_name(seq));
    return TF_ERR;
}

tf_ret tf_pop_front(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_DEQUE)) return TF_ERR;
    tf_obj *deque = tf_stack_peek(ctx, 0);
    if (deque->deque.len == 0) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected non-empty deque\n",
                              ctx->current_word);
        return TF_ERR;
    }

    deque = tf_stack_pop_type(ctx, TF_OBJ_TYPE_DEQUE);
    tf_obj *result = deque->refcount == 1 ? deque : tf_deque_clone(deque);
    tf_obj *item = tf_deque_pop_front(result);
    tf_stack_push(ctx, result);
    tf_stack_push(ctx, item);
    if (result != deque) tf_obj_release(deque);
    return TF_OK;
}

tf_ret tf_pop_back(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *coll = tf_stack_peek(ctx, 0);
    size_t len = 0;
    if (tf_obj_typeof(coll) == TF_OBJ_TYPE_VECTOR) {
        len = coll->vector.len;
    } else if (tf_obj_typeof(coll) == TF_OBJ_TYPE_DEQUE) {
        len = coll->deque.len;
    } else {
        tf_ctx_runtime_errorf(
            ctx, "'%s' expected vector or deque at stack depth 0, found %s\n",
            ctx->current_word, tf_obj_type_name(coll));
        return TF_ERR;
    }
    if (len == 0) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected non-empty vector or deque\n",
                              ctx->current_word);
        return TF_ERR;
    }

    coll = tf_stack_pop(ctx);
    tf_obj *result = coll;
    tf_obj *item = NULL;
    if (tf_obj_typeof(coll) == TF_OBJ_TYPE_VECTOR) {
        if (coll->refcount != 1) result = tf_vector_clone(coll);
        item = tf_vector_pop(result);
    } else {
        if (coll->refcount != 1) result = tf_deque_clone(coll);
        item = tf_deque_pop_back(result);
    }
    tf_stack_push(ctx, result);
    tf_stack_push(ctx, item);
    if (result != coll) tf_obj_release(coll);
    return TF_OK;
}

tf_ret tf_pq_push(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 2, TF_OBJ_TYPE_PQUEUE) ||
        !tf_ctx_require_number(ctx, 1) ||
        !tf_ctx_require_stack(ctx, 3)) {
        return TF_ERR;
    }

    tf_obj *priority_obj = tf_stack_peek(ctx, 1);
    if (!valid_priority(ctx, priority_obj)) return TF_ERR;

    tf_obj *value = tf_stack_pop(ctx);
    priority_obj = tf_stack_pop(ctx);
    tf_obj *pqueue = tf_stack_pop_type(ctx, TF_OBJ_TYPE_PQUEUE);
    tf_obj *result = pqueue->refcount == 1 ? pqueue : tf_pqueue_clone(pqueue);
    tf_pqueue_push(result, priority_obj, value);
    tf_stack_push(ctx, result);
    tf_obj_release(value);
    tf_obj_release(priority_obj);
    if (result != pqueue) tf_obj_release(pqueue);
    return TF_OK;
}

tf_ret tf_pq_peek(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_PQUEUE)) return TF_ERR;
    tf_obj *pqueue = tf_stack_peek(ctx, 0);
    tf_obj *priority = NULL;
    tf_obj *value = NULL;
    if (!tf_pqueue_peek(pqueue, &priority, &value)) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected non-empty pqueue\n",
                              ctx->current_word);
        return TF_ERR;
    }

    tf_obj_retain(priority);
    tf_obj_retain(value);
    tf_stack_push(ctx, priority);
    tf_stack_push(ctx, value);
    return TF_OK;
}

tf_ret tf_pq_pop(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_PQUEUE)) return TF_ERR;
    tf_obj *pqueue = tf_stack_peek(ctx, 0);
    if (pqueue->pqueue.len == 0) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected non-empty pqueue\n",
                              ctx->current_word);
        return TF_ERR;
    }

    pqueue = tf_stack_pop_type(ctx, TF_OBJ_TYPE_PQUEUE);
    tf_obj *result = pqueue->refcount == 1 ? pqueue : tf_pqueue_clone(pqueue);
    tf_obj *priority = NULL;
    tf_obj *value = NULL;
    tf_pqueue_pop(result, &priority, &value);
    tf_stack_push(ctx, result);
    tf_stack_push(ctx, priority);
    tf_stack_push(ctx, value);
    if (result != pqueue) tf_obj_release(pqueue);
    return TF_OK;
}

tf_ret tf_join(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 2) ||
        !tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_STR)) {
        return TF_ERR;
    }
    tf_obj *sep = tf_stack_peek(ctx, 0);
    tf_obj *seq = tf_stack_peek(ctx, 1);
    tf_type type = tf_obj_typeof(seq);
    if (type != TF_OBJ_TYPE_VECTOR && type != TF_OBJ_TYPE_LIST) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected vector or list at stack depth 1, found %s\n",
                              ctx->current_word, tf_obj_type_name(seq));
        return TF_ERR;
    }

    size_t total_len = 0;
    size_t len = sequence_len(seq);
    tf_sequence_iter iter;
    tf_sequence_iter_init(&iter, seq);
    for (size_t i = 0;; i++) {
        tf_obj *elem = tf_sequence_iter_next_owned(&iter);
        if (!elem) break;
        if (tf_obj_typeof(elem) != TF_OBJ_TYPE_STR) {
            tf_ctx_runtime_errorf(
                ctx, "'%s' expected item %zu to be string, found %s\n",
                ctx->current_word, i, tf_obj_type_name(elem));
            tf_obj_release(elem);
            return TF_ERR;
        }
        total_len += elem->str.len;
        tf_obj_release(elem);
    }
    if (len > 1) { total_len += sep->str.len * (len - 1); }

    sep = tf_stack_pop(ctx);
    seq = tf_stack_pop(ctx);

    tf_obj *result = tf_obj_new_string_uninitialized(total_len);
    char *p = result->str.ptr;
    tf_sequence_iter_init(&iter, seq);
    for (size_t i = 0;; i++) {
        tf_obj *elem = tf_sequence_iter_next_owned(&iter);
        if (!elem) break;
        memcpy(p, elem->str.ptr, elem->str.len);
        p += elem->str.len;
        if (i + 1 < len) {
            memcpy(p, sep->str.ptr, sep->str.len);
            p += sep->str.len;
        }
        tf_obj_release(elem);
    }
    tf_stack_push(ctx, result);
    tf_obj_release(seq);
    tf_obj_release(sep);
    return TF_OK;
}

tf_ret tf_trim(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_STR)) return TF_ERR;
    tf_obj *str = tf_stack_pop_type(ctx, TF_OBJ_TYPE_STR);

    if (str->str.len == 0) {
        tf_stack_push(ctx, tf_obj_new_string("", 0));
        tf_obj_release(str);
        return TF_OK;
    }

    char *start = str->str.ptr;
    char *end = str->str.ptr + str->str.len - 1;

    while (start <= end && isspace((unsigned char)*start)) start++;
    while (end > start && isspace((unsigned char)*end)) end--;

    size_t new_len = (start <= end) ? (size_t)(end - start + 1) : 0;
    tf_stack_push(ctx, tf_obj_new_string(start, new_len));
    tf_obj_release(str);
    return TF_OK;
}

tf_ret tf_starts_with_q(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_STR) ||
        !tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_STR)) {
        return TF_ERR;
    }
    tf_obj *prefix = tf_stack_pop_type(ctx, TF_OBJ_TYPE_STR);
    tf_obj *string = tf_stack_pop_type(ctx, TF_OBJ_TYPE_STR);
    bool result = prefix->str.len <= string->str.len &&
                  memcmp(string->str.ptr, prefix->str.ptr,
                         prefix->str.len) == 0;
    tf_stack_push(ctx, tf_obj_new_bool(result));
    tf_obj_release(string);
    tf_obj_release(prefix);
    return TF_OK;
}

tf_ret tf_ends_with_q(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_STR) ||
        !tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_STR)) {
        return TF_ERR;
    }
    tf_obj *suffix = tf_stack_pop_type(ctx, TF_OBJ_TYPE_STR);
    tf_obj *string = tf_stack_pop_type(ctx, TF_OBJ_TYPE_STR);
    bool result = suffix->str.len <= string->str.len &&
                  memcmp(string->str.ptr + string->str.len - suffix->str.len,
                         suffix->str.ptr, suffix->str.len) == 0;
    tf_stack_push(ctx, tf_obj_new_bool(result));
    tf_obj_release(string);
    tf_obj_release(suffix);
    return TF_OK;
}

tf_ret tf_replace(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 2, TF_OBJ_TYPE_STR) ||
        !tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_STR) ||
        !tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_STR)) {
        return TF_ERR;
    }
    tf_obj *string = tf_stack_peek(ctx, 2);
    tf_obj *old = tf_stack_peek(ctx, 1);
    tf_obj *replacement = tf_stack_peek(ctx, 0);
    if (old->str.len == 0) {
        tf_ctx_runtime_errorf(ctx, "'replace' expected a non-empty substring\n");
        return TF_ERR;
    }

    size_t count = 0;
    size_t position = 0;
    while (old->str.len <= string->str.len - position) {
        if (memcmp(string->str.ptr + position, old->str.ptr,
                   old->str.len) == 0) {
            count++;
            position += old->str.len;
        } else {
            position++;
        }
    }
    size_t result_len = string->str.len;
    if (replacement->str.len > old->str.len) {
        size_t growth = replacement->str.len - old->str.len;
        if (count > (SIZE_MAX - result_len) / growth) {
            tf_ctx_runtime_errorf(ctx, "'replace' result is too large\n");
            return TF_ERR;
        }
        result_len += count * growth;
    } else {
        result_len -= count * (old->str.len - replacement->str.len);
    }

    replacement = tf_stack_pop_type(ctx, TF_OBJ_TYPE_STR);
    old = tf_stack_pop_type(ctx, TF_OBJ_TYPE_STR);
    string = tf_stack_pop_type(ctx, TF_OBJ_TYPE_STR);
    tf_obj *result = tf_obj_new_string_uninitialized(result_len);
    size_t input = 0;
    size_t output = 0;
    while (input < string->str.len) {
        if (old->str.len <= string->str.len - input &&
            memcmp(string->str.ptr + input, old->str.ptr, old->str.len) == 0) {
            memcpy(result->str.ptr + output, replacement->str.ptr,
                   replacement->str.len);
            input += old->str.len;
            output += replacement->str.len;
        } else {
            result->str.ptr[output++] = string->str.ptr[input++];
        }
    }
    tf_stack_push(ctx, result);
    tf_obj_release(string);
    tf_obj_release(old);
    tf_obj_release(replacement);
    return TF_OK;
}

tf_ret tf_upper(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_STR)) return TF_ERR;
    tf_obj *str = tf_stack_pop_type(ctx, TF_OBJ_TYPE_STR);

    tf_obj *result = tf_obj_new_string_uninitialized(str->str.len);
    for (size_t i = 0; i < str->str.len; i++) {
        result->str.ptr[i] = (char)toupper((unsigned char)str->str.ptr[i]);
    }

    tf_stack_push(ctx, result);
    tf_obj_release(str);
    return TF_OK;
}

tf_ret tf_lower(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_STR)) return TF_ERR;
    tf_obj *str = tf_stack_pop_type(ctx, TF_OBJ_TYPE_STR);

    tf_obj *result = tf_obj_new_string_uninitialized(str->str.len);
    for (size_t i = 0; i < str->str.len; i++) {
        result->str.ptr[i] = (char)tolower((unsigned char)str->str.ptr[i]);
    }

    tf_stack_push(ctx, result);
    tf_obj_release(str);
    return TF_OK;
}

tf_ret tf_splitmid(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 0)) return TF_ERR;

    tf_obj *seq = tf_stack_pop(ctx);

    size_t len = sequence_len(seq);
    size_t mid = len / 2;

    if (tf_obj_typeof(seq) == TF_OBJ_TYPE_STR) {
        tf_stack_push(ctx, tf_obj_new_string(seq->str.ptr, mid));
        tf_stack_push(ctx, tf_obj_new_string(seq->str.ptr + mid, len - mid));
    } else if (tf_obj_typeof(seq) == TF_OBJ_TYPE_LIST) {
        tf_stack_push(ctx, tf_list_take_obj(seq, mid));
        tf_stack_push(ctx, tf_list_drop_obj(seq, mid));
    } else {
        tf_obj *left_items = tf_obj_new_vector_with_capacity(mid);
        tf_obj *right_items = tf_obj_new_vector_with_capacity(len - mid);
        push_sequence_items_to_vector(left_items, seq, 0, mid);
        push_sequence_items_to_vector(right_items, seq, mid, len);

        tf_stack_push(ctx, left_items);
        tf_stack_push(ctx, right_items);
    }

    tf_obj_release(seq);
    return TF_OK;
}

tf_ret tf_range(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_INT) ||
        !tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_INT)) {
        return TF_ERR;
    }
    tf_obj *end_obj = tf_stack_peek(ctx, 0);
    tf_obj *start_obj = tf_stack_peek(ctx, 1);

    int64_t start = tf_obj_int_value(start_obj);
    int64_t end = tf_obj_int_value(end_obj);
    if (end < start) {
        tf_ctx_runtime_errorf(ctx, "'%s' end must be greater than or equal to start\n",
                              ctx->current_word);
        return TF_ERR;
    }

    end_obj = tf_stack_pop(ctx);
    start_obj = tf_stack_pop(ctx);

    uint64_t count64 = (uint64_t)end - (uint64_t)start;
    if (count64 > SIZE_MAX / sizeof(tf_obj *)) {
        tf_obj_release(end_obj);
        tf_obj_release(start_obj);
        tf_ctx_runtime_errorf(ctx, "'%s' range is too large\n",
                              ctx->current_word);
        return TF_ERR;
    }
    size_t count = (size_t)count64;
    tf_obj *result = tf_obj_new_vector_with_capacity(count);
    int64_t value = start;
    for (size_t i = 0; i < count; i++, value++) {
        tf_vector_push(result, tf_obj_new_int(value));
    }

    tf_stack_push(ctx, result);
    tf_obj_release(end_obj);
    tf_obj_release(start_obj);
    return TF_OK;
}

tf_ret tf_empty_q(tf_ctx *ctx) {
    if (!require_countable(ctx, 0)) return TF_ERR;
    tf_obj *coll = tf_stack_pop(ctx);
    bool is_empty = false;
    if (tf_obj_typeof(coll) == TF_OBJ_TYPE_VECTOR) {
        is_empty = coll->vector.len == 0;
    } else if (tf_obj_typeof(coll) == TF_OBJ_TYPE_LIST) {
        is_empty = coll->list.len == 0;
    } else if (tf_obj_typeof(coll) == TF_OBJ_TYPE_STR) {
        is_empty = coll->str.len == 0;
    } else if (tf_obj_typeof(coll) == TF_OBJ_TYPE_MAP) {
        is_empty = coll->map.len == 0;
    } else if (tf_obj_typeof(coll) == TF_OBJ_TYPE_SET) {
        is_empty = coll->set.len == 0;
    } else if (tf_obj_typeof(coll) == TF_OBJ_TYPE_DEQUE) {
        is_empty = coll->deque.len == 0;
    } else if (tf_obj_typeof(coll) == TF_OBJ_TYPE_PQUEUE) {
        is_empty = coll->pqueue.len == 0;
    }
    tf_stack_push(ctx, tf_obj_new_bool(is_empty));
    tf_obj_release(coll);
    return TF_OK;
}

tf_ret tf_char_q(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
    tf_obj *o = tf_stack_pop(ctx);
    tf_stack_push(ctx, tf_obj_new_bool(is_char_obj(o)));
    tf_obj_release(o);
    return TF_OK;
}

tf_ret tf_to_char(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_INT)) return TF_ERR;
    tf_obj *value = tf_stack_peek(ctx, 0);
    int64_t integer = tf_obj_int_value(value);
    if (integer < 0 || integer > 255) {
        tf_ctx_runtime_errorf(ctx,
                              "'%s' expected an integer from 0 through 255\n",
                              ctx->current_word);
        return TF_ERR;
    }

    value = tf_stack_pop_type(ctx, TF_OBJ_TYPE_INT);
    unsigned char byte = (unsigned char)integer;
    tf_stack_push(ctx, tf_obj_new_string((const char *)&byte, 1));
    tf_obj_release(value);
    return TF_OK;
}

tf_ret tf_char_code(tf_ctx *ctx) {
    if (!require_char(ctx, 0)) return TF_ERR;
    tf_obj *value = tf_stack_pop_type(ctx, TF_OBJ_TYPE_STR);
    tf_stack_push(ctx, tf_obj_new_int((unsigned char)value->str.ptr[0]));
    tf_obj_release(value);
    return TF_OK;
}

tf_ret tf_letter_q(tf_ctx *ctx) {
    return char_predicate(ctx, TF_CHAR_PRED_LETTER);
}

tf_ret tf_digit_q(tf_ctx *ctx) {
    return char_predicate(ctx, TF_CHAR_PRED_DIGIT);
}

tf_ret tf_alnum_q(tf_ctx *ctx) {
    return char_predicate(ctx, TF_CHAR_PRED_ALNUM);
}

tf_ret tf_space_q(tf_ctx *ctx) {
    return char_predicate(ctx, TF_CHAR_PRED_SPACE);
}

tf_ret tf_upper_q(tf_ctx *ctx) {
    return char_predicate(ctx, TF_CHAR_PRED_UPPER);
}

tf_ret tf_lower_q(tf_ctx *ctx) {
    return char_predicate(ctx, TF_CHAR_PRED_LOWER);
}

tf_ret tf_punct_q(tf_ctx *ctx) {
    return char_predicate(ctx, TF_CHAR_PRED_PUNCT);
}

#include "tf_builtins.h"
#include <string.h>
#include <stdlib.h>
#include "tf_exec.h"
#include "tf_alloc.h"
#include "tf_obj.h"

#define TF_CONTROL_STATE_BLOCK_SIZE 512
#define TF_CONTROL_STATE_CACHE_LIMIT 128
#define TF_FILTER_VECTOR_RESERVE_CAP 64
#define TF_STACK_SNAPSHOT_INLINE 32
#define TF_ASSERT_CACHED_CONTROL_STATE(type)                                  \
    _Static_assert(sizeof(type) <= TF_CONTROL_STATE_BLOCK_SIZE,               \
                   #type " must fit in the control-state cache block")

typedef struct control_state_cache_node {
    struct control_state_cache_node *next;
} control_state_cache_node;

static control_state_cache_node *control_state_cache = NULL;
static size_t control_state_cache_len = 0;

static void *control_state_acquire(size_t size) {
    if (size > TF_CONTROL_STATE_BLOCK_SIZE) return tf_xmalloc(size);
    if (!control_state_cache) return tf_xmalloc(TF_CONTROL_STATE_BLOCK_SIZE);
    control_state_cache_node *node = control_state_cache;
    control_state_cache = node->next;
    control_state_cache_len--;
    return node;
}

static void control_state_release(void *ptr, size_t size) {
    if (size > TF_CONTROL_STATE_BLOCK_SIZE ||
        control_state_cache_len >= TF_CONTROL_STATE_CACHE_LIMIT) {
        free(ptr);
        return;
    }
    control_state_cache_node *node = ptr;
    node->next = control_state_cache;
    control_state_cache = node;
    control_state_cache_len++;
}

void tf_control_state_cache_clear(void) {
    while (control_state_cache) {
        control_state_cache_node *next = control_state_cache->next;
        free(control_state_cache);
        control_state_cache = next;
    }
    control_state_cache_len = 0;
}

tf_ret tf_exec(tf_ctx *ctx) {
    if (!tf_ctx_require_callable(ctx, 0)) return TF_ERR;
    tf_obj *o = tf_stack_pop_callable(ctx);

    tf_ret res = tf_vm_call_callable(ctx, o);
    tf_obj_release(o);
    return res;
}

static void restore_stack_copy(tf_ctx *ctx, tf_obj **saved_stack,
                               size_t saved_len);

typedef struct {
    tf_obj **items;
    size_t len;
} stack_snapshot;

static inline void stack_snapshot_save(tf_ctx *ctx,
                                       stack_snapshot *snapshot,
                                       tf_obj **inline_items);
static inline void stack_snapshot_restore(
    tf_ctx *ctx, const stack_snapshot *snapshot);
static inline void stack_snapshot_release(tf_ctx *ctx,
                                          stack_snapshot *snapshot,
                                          tf_obj **inline_items);

/* Snapshots implement successful isolation between applications. Unwind
 * cleanup releases them without restoring; only try_error establishes a
 * recoverable transaction boundary. */

typedef enum { TF_APP2_LEFT, TF_APP2_RIGHT, TF_APP2_DONE } app2_stage;

typedef struct {
    tf_obj *callable;
    tf_obj *left;
    tf_obj *right;
    tf_obj *left_result;
    stack_snapshot stack;
    app2_stage stage;
    tf_obj *inline_stack[TF_STACK_SNAPSHOT_INLINE];
} app2_state;

TF_ASSERT_CACHED_CONTROL_STATE(app2_state);

static tf_ret app2_step(tf_ctx *ctx, void *state, bool *done) {
    app2_state *s = state;
    if (s->stage == TF_APP2_LEFT) {
        tf_obj_retain(s->left);
        tf_stack_push(ctx, s->left);
        s->stage = TF_APP2_RIGHT;
        *done = false;
        return tf_vm_call_callable(ctx, s->callable);
    }
    if (s->stage == TF_APP2_RIGHT) {
        if (tf_stack_len(ctx) != s->stack.len + 1) {
            tf_ctx_runtime_errorf(
                ctx,
                "'app2' expected the callable to consume one item and leave "
                "one result\n");
            return TF_ERR;
        }
        s->left_result = tf_stack_pop(ctx);
        stack_snapshot_restore(ctx, &s->stack);
        tf_obj_retain(s->right);
        tf_stack_push(ctx, s->right);
        s->stage = TF_APP2_DONE;
        *done = false;
        return tf_vm_call_callable(ctx, s->callable);
    }

    if (tf_stack_len(ctx) != s->stack.len + 1) {
        tf_ctx_runtime_errorf(
            ctx,
            "'app2' expected the callable to consume one item and leave one "
            "result\n");
        return TF_ERR;
    }
    tf_obj *right_result = tf_stack_pop(ctx);
    stack_snapshot_restore(ctx, &s->stack);
    tf_stack_push(ctx, s->left_result);
    s->left_result = NULL;
    tf_stack_push(ctx, right_result);
    *done = true;
    return TF_OK;
}

static void app2_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    app2_state *s = state;
    (void)status;
    stack_snapshot_release(ctx, &s->stack, s->inline_stack);
    tf_obj_release(s->callable);
    tf_obj_release(s->left);
    tf_obj_release(s->right);
    if (s->left_result) tf_obj_release(s->left_result);
    control_state_release(s, sizeof(*s));
}

tf_ret tf_app2(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 3) || !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }

    app2_state *state = control_state_acquire(sizeof(*state));
    state->callable = tf_stack_pop(ctx);
    state->right = tf_stack_pop(ctx);
    state->left = tf_stack_pop(ctx);
    state->left_result = NULL;
    stack_snapshot_save(ctx, &state->stack, state->inline_stack);
    state->stage = TF_APP2_LEFT;
    tf_frame_push_native(ctx, app2_step, app2_cleanup, state);

    return TF_OK;
}

static void restore_stack_owned(tf_ctx *ctx, tf_obj **saved_stack,
                                size_t saved_len) {
    while (tf_stack_len(ctx) > 0) {
        tf_obj *o = tf_stack_pop(ctx);
        tf_obj_release(o);
    }
    for (size_t i = 0; i < saved_len; i++) tf_stack_push(ctx, saved_stack[i]);
}

static void restore_stack_copy(tf_ctx *ctx, tf_obj **saved_stack, size_t saved_len) {
    while (tf_stack_len(ctx) > 0) {
        tf_obj *o = tf_stack_pop(ctx);
        tf_obj_release(o);
    }
    for (size_t i = 0; i < saved_len; i++) {
        tf_stack_push(ctx, saved_stack[i]);
        tf_obj_retain(saved_stack[i]);
    }
}

static void push_retained(tf_obj *vector, tf_obj *elem) {
    tf_obj_retain(elem);
    tf_vector_push(vector, elem);
}

// Sequence combinators treat strings as byte sequences. A string item is a
// one-byte string so the same callable protocol works for vectors, lists, and
// strings.
static bool is_sequence(tf_obj *o) {
    tf_type type = tf_obj_typeof(o);
    return type == TF_OBJ_TYPE_VECTOR || type == TF_OBJ_TYPE_LIST ||
           type == TF_OBJ_TYPE_STR;
}

static bool require_condition(tf_ctx *ctx, size_t depth) {
    if (!tf_ctx_require_stack(ctx, depth + 1)) return false;
    tf_obj *o = tf_stack_peek(ctx, depth);
    if (tf_obj_typeof(o) == TF_OBJ_TYPE_BOOL || tf_obj_is_callable(o)) {
        return true;
    }

    tf_ctx_runtime_errorf(
        ctx, "'%s' expected bool or callable at stack depth %zu, found %s\n",
        ctx->current_word, depth, tf_obj_type_name(o));
    return false;
}

static bool require_callable_vector(tf_ctx *ctx, tf_obj *vector, const char *word) {
    if (tf_obj_typeof(vector) != TF_OBJ_TYPE_VECTOR) {
        tf_ctx_runtime_errorf(ctx, "'%s' expected vector, found %s\n", word,
                              tf_obj_type_name(vector));
        return false;
    }
    for (size_t i = 0; i < vector->vector.len; i++) {
        if (!tf_obj_is_callable(vector->vector.elem[i])) {
            tf_ctx_runtime_errorf(
                ctx, "'%s' expected vector item %zu to be callable, found %s\n",
                word, i, tf_obj_type_name(vector->vector.elem[i]));
            return false;
        }
    }
    return true;
}

static size_t sequence_len(tf_obj *seq) {
    if (seq->type == TF_OBJ_TYPE_VECTOR) return seq->vector.len;
    if (seq->type == TF_OBJ_TYPE_LIST) return seq->list.len;
    return seq->str.len;
}

static bool is_char_string(tf_obj *o) {
    return tf_obj_typeof(o) == TF_OBJ_TYPE_STR && o->str.len == 1;
}

typedef struct {
    char *ptr;
    size_t len;
    size_t cap;
} bytebuf;

static void bytebuf_init(bytebuf *buf, size_t cap) {
    buf->len = 0;
    buf->cap = cap > 0 ? cap : 1;
    buf->ptr = tf_xmalloc(buf->cap + 1);
    buf->ptr[0] = '\0';
}

static void bytebuf_append(bytebuf *buf, char c) {
    if (buf->len >= buf->cap) {
        buf->cap *= 2;
        buf->ptr = tf_xrealloc(buf->ptr, buf->cap + 1);
    }
    buf->ptr[buf->len++] = c;
    buf->ptr[buf->len] = '\0';
}

static tf_obj *bytebuf_to_string(bytebuf *buf) {
    tf_obj *result = tf_obj_new_string_take(buf->ptr, buf->len);
    buf->ptr = NULL;
    buf->len = 0;
    buf->cap = 0;
    return result;
}

static size_t stack_results_after(tf_ctx *ctx, size_t base_len) {
    size_t len = tf_stack_len(ctx);
    return len > base_len ? len - base_len : 0;
}

static inline void stack_snapshot_save(tf_ctx *ctx, stack_snapshot *snapshot,
                                       tf_obj **inline_items) {
    snapshot->len = tf_stack_len(ctx);
    if (snapshot->len == 0) {
        snapshot->items = NULL;
    } else if (snapshot->len <= TF_STACK_SNAPSHOT_INLINE) {
        snapshot->items = inline_items;
    } else {
        snapshot->items =
            tf_scratch_alloc(ctx, sizeof(tf_obj *) * snapshot->len);
    }
    for (size_t i = 0; i < snapshot->len; i++) {
        snapshot->items[i] = tf_stack_peek(ctx, snapshot->len - 1 - i);
        tf_obj_retain(snapshot->items[i]);
    }
}

static inline void stack_snapshot_restore(
    tf_ctx *ctx, const stack_snapshot *snapshot) {
    restore_stack_copy(ctx, snapshot->items, snapshot->len);
}

static inline void stack_snapshot_release(tf_ctx *ctx,
                                          stack_snapshot *snapshot,
                                          tf_obj **inline_items) {
    for (size_t i = 0; i < snapshot->len; i++) {
        tf_obj_release(snapshot->items[i]);
    }
    if (snapshot->items != inline_items) {
        tf_scratch_release(ctx, snapshot->items);
    }
    snapshot->items = NULL;
    snapshot->len = 0;
}

static void clear_stack(tf_ctx *ctx) {
    while (tf_stack_len(ctx) > 0) {
        tf_obj *o = tf_stack_pop(ctx);
        tf_obj_release(o);
    }
}

static tf_ret collect_outputs(tf_ctx *ctx, size_t base_len, tf_obj *outputs) {
    size_t len = tf_stack_len(ctx);
    if (len < base_len) return TF_ERR;
    tf_vector_reserve(outputs, outputs->vector.len + len - base_len);
    for (size_t i = base_len; i < len; i++) {
        push_retained(outputs, ctx->data_stack->vector.elem[i]);
    }
    return TF_OK;
}

typedef struct {
    tf_obj *body;
    int64_t remaining;
} times_state;

static tf_ret times_step(tf_ctx *ctx, void *state, bool *done) {
    times_state *s = state;
    if (s->remaining <= 0) {
        *done = true;
        return TF_OK;
    }

    s->remaining--;
    *done = false;
    return tf_vm_call_callable(ctx, s->body);
}

static void times_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    (void)ctx;
    (void)status;
    times_state *s = state;
    tf_obj_release(s->body);
    control_state_release(s, sizeof(*s));
}

typedef struct {
    tf_obj *body;
    tf_obj *data;
    tf_sequence_iter iter;
} each_state;

static tf_ret each_step(tf_ctx *ctx, void *state, bool *done) {
    each_state *s = state;
    tf_obj *item = tf_sequence_iter_next_owned(&s->iter);
    if (!item) {
        *done = true;
        return TF_OK;
    }

    tf_stack_push(ctx, item);
    *done = false;
    return tf_vm_call_callable(ctx, s->body);
}

static void each_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    (void)ctx;
    (void)status;
    each_state *s = state;
    tf_obj_release(s->body);
    tf_obj_release(s->data);
    control_state_release(s, sizeof(*s));
}

/* The dip, keep, fold, and bi continuations thread the ambient stack through
 * their bodies. They keep only hidden inputs and result-shape marks; an
 * enclosing try owns recoverable stack restoration. Retaining the ambient
 * stack here would also turn unique collection updates into copies. */

typedef struct {
    tf_obj *body;
    tf_obj *saved;
    bool scheduled;
} dip_state;

TF_ASSERT_CACHED_CONTROL_STATE(dip_state);

static tf_ret dip_step(tf_ctx *ctx, void *state, bool *done) {
    dip_state *s = state;
    if (!s->scheduled) {
        s->scheduled = true;
        *done = false;
        return tf_vm_call_callable(ctx, s->body);
    }

    tf_stack_push(ctx, s->saved);
    s->saved = NULL;
    *done = true;
    return TF_OK;
}

static void dip_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    (void)ctx;
    (void)status;
    dip_state *s = state;
    tf_obj_release(s->body);
    if (s->saved) tf_obj_release(s->saved);
    control_state_release(s, sizeof(*s));
}

typedef struct {
    tf_obj *body;
    tf_obj *saved;
    size_t base_len;
    bool scheduled;
} keep_state;

TF_ASSERT_CACHED_CONTROL_STATE(keep_state);

static tf_ret keep_step(tf_ctx *ctx, void *state, bool *done) {
    keep_state *s = state;
    if (!s->scheduled) {
        s->scheduled = true;
        tf_obj_retain(s->saved);
        tf_stack_push(ctx, s->saved);
        *done = false;
        return tf_vm_call_callable(ctx, s->body);
    }

    if (tf_stack_len(ctx) < s->base_len) return TF_ERR;

    size_t out_len = tf_stack_len(ctx) - s->base_len;
    tf_obj **outputs = out_len > 0 ? tf_xmalloc(sizeof(tf_obj *) * out_len) : NULL;
    for (size_t i = out_len; i > 0; i--) outputs[i - 1] = tf_stack_pop(ctx);

    tf_stack_push(ctx, s->saved);
    s->saved = NULL;
    for (size_t i = 0; i < out_len; i++) tf_stack_push(ctx, outputs[i]);

    free(outputs);
    *done = true;
    return TF_OK;
}

static void keep_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    (void)ctx;
    (void)status;
    keep_state *s = state;
    tf_obj_release(s->body);
    if (s->saved) tf_obj_release(s->saved);
    control_state_release(s, sizeof(*s));
}

typedef struct {
    tf_obj *body;
    tf_obj *data;
    size_t base_len;
    tf_sequence_iter iter;
    bool awaiting_body;
} fold_state;

TF_ASSERT_CACHED_CONTROL_STATE(fold_state);

static tf_ret fold_step(tf_ctx *ctx, void *state, bool *done) {
    fold_state *s = state;

    if (s->awaiting_body) {
        if (tf_stack_len(ctx) != s->base_len + 1) {
            tf_ctx_runtime_errorf(
                ctx,
                "'fold' expected body to consume accumulator and item and leave one "
                "accumulator, found %zu value(s) above the saved stack\n",
                stack_results_after(ctx, s->base_len));
            return TF_ERR;
        }
        s->awaiting_body = false;
    }

    tf_obj *item = tf_sequence_iter_next_owned(&s->iter);
    if (!item) {
        *done = true;
        return TF_OK;
    }

    tf_stack_push(ctx, item);
    s->awaiting_body = true;
    *done = false;
    return tf_vm_call_callable(ctx, s->body);
}

static void fold_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    (void)ctx;
    (void)status;
    fold_state *s = state;
    tf_obj_release(s->body);
    tf_obj_release(s->data);
    control_state_release(s, sizeof(*s));
}

typedef struct {
    tf_obj *body;
    tf_obj *data;
    stack_snapshot stack;
    tf_sequence_iter iter;
    bool awaiting_body;
    bool string_result;
    bool list_result;
    tf_obj *vector_result;
    tf_list_builder list_builder;
    bytebuf str_result;
    tf_obj *inline_stack[TF_STACK_SNAPSHOT_INLINE];
} map_state;

TF_ASSERT_CACHED_CONTROL_STATE(map_state);

static tf_ret map_step(tf_ctx *ctx, void *state, bool *done) {
    map_state *s = state;

    if (s->awaiting_body) {
        if (tf_stack_len(ctx) != s->stack.len + 1) {
            tf_ctx_runtime_errorf(
                ctx,
                "'map' expected body to consume one item and leave one result, "
                "found %zu value(s) above the saved stack\n",
                stack_results_after(ctx, s->stack.len));
            return TF_ERR;
        }

        tf_obj *mapped = tf_stack_pop(ctx);
        if (s->string_result) {
            if (!is_char_string(mapped)) {
                tf_ctx_runtime_errorf(ctx,
                                      "'map' expected string body to return a "
                                      "one-byte string, found %s\n",
                                      tf_obj_type_name(mapped));
                tf_obj_release(mapped);
                return TF_ERR;
            }
            bytebuf_append(&s->str_result, mapped->str.ptr[0]);
            tf_obj_release(mapped);
        } else if (s->list_result) {
            tf_list_builder_push_owned(&s->list_builder, mapped);
        } else {
            tf_vector_push(s->vector_result, mapped);
        }
        stack_snapshot_restore(ctx, &s->stack);
        s->awaiting_body = false;
    }

    tf_obj *item = tf_sequence_iter_next_owned(&s->iter);
    if (!item) {
        if (s->string_result) {
            tf_stack_push(ctx, bytebuf_to_string(&s->str_result));
            free(s->str_result.ptr);
            s->str_result.ptr = NULL;
        } else if (s->list_result) {
            tf_stack_push(ctx, tf_list_builder_finish(&s->list_builder));
        } else {
            tf_stack_push(ctx, s->vector_result);
            s->vector_result = NULL;
        }
        *done = true;
        return TF_OK;
    }

    tf_stack_push(ctx, item);
    s->awaiting_body = true;
    *done = false;
    return tf_vm_call_callable(ctx, s->body);
}

static void map_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    map_state *s = state;
    (void)status;
    stack_snapshot_release(ctx, &s->stack, s->inline_stack);
    tf_obj_release(s->body);
    tf_obj_release(s->data);
    if (s->string_result) {
        free(s->str_result.ptr);
    } else if (s->list_result) {
        tf_list_builder_cleanup(&s->list_builder);
    } else if (s->vector_result) {
        tf_obj_release(s->vector_result);
    }
    control_state_release(s, sizeof(*s));
}

typedef struct {
    tf_obj *body;
    stack_snapshot stack;
    int64_t remaining;
    bool awaiting_body;
    tf_obj *result;
    tf_obj *inline_stack[TF_STACK_SNAPSHOT_INLINE];
} replicate_state;

TF_ASSERT_CACHED_CONTROL_STATE(replicate_state);

static tf_ret replicate_step(tf_ctx *ctx, void *state, bool *done) {
    replicate_state *s = state;

    if (s->awaiting_body) {
        if (tf_stack_len(ctx) != s->stack.len + 1) {
            tf_ctx_runtime_errorf(ctx,
                                  "'replicate' expected body to leave one result, "
                                  "found %zu value(s) above the saved stack\n",
                                  stack_results_after(ctx, s->stack.len));
            return TF_ERR;
        }
        tf_obj *item = tf_stack_pop(ctx);
        tf_vector_push(s->result, item);
        stack_snapshot_restore(ctx, &s->stack);
        s->awaiting_body = false;
    }

    if (s->remaining <= 0) {
        tf_stack_push(ctx, s->result);
        s->result = NULL;
        *done = true;
        return TF_OK;
    }

    s->remaining--;
    s->awaiting_body = true;
    *done = false;
    return tf_vm_call_callable(ctx, s->body);
}

static void replicate_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    replicate_state *s = state;
    (void)status;
    stack_snapshot_release(ctx, &s->stack, s->inline_stack);
    tf_obj_release(s->body);
    if (s->result) tf_obj_release(s->result);
    control_state_release(s, sizeof(*s));
}

typedef struct {
    tf_obj *body;
    tf_obj *data;
    stack_snapshot stack;
    bool scheduled;
    tf_obj *result;
    tf_obj *inline_stack[TF_STACK_SNAPSHOT_INLINE];
} infra_state;

TF_ASSERT_CACHED_CONTROL_STATE(infra_state);

static tf_ret infra_step(tf_ctx *ctx, void *state, bool *done) {
    infra_state *s = state;
    if (!s->scheduled) {
        s->scheduled = true;
        *done = false;
        return tf_vm_call_callable(ctx, s->body);
    }

    s->result = tf_obj_new_vector();
    tf_ret res = collect_outputs(ctx, 0, s->result);
    if (res != TF_OK) return res;

    stack_snapshot_restore(ctx, &s->stack);
    tf_stack_push(ctx, s->result);
    s->result = NULL;
    *done = true;
    return TF_OK;
}

static void infra_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    infra_state *s = state;
    (void)status;
    stack_snapshot_release(ctx, &s->stack, s->inline_stack);
    tf_obj_release(s->body);
    tf_obj_release(s->data);
    if (s->result) tf_obj_release(s->result);
    control_state_release(s, sizeof(*s));
}

typedef struct {
    tf_obj *branches;
    tf_obj *value;
    stack_snapshot stack;
    tf_obj *outputs;
    size_t index;
    bool awaiting_branch;
    bool construct_result;
    tf_obj *inline_stack[TF_STACK_SNAPSHOT_INLINE];
} cleave_state;

TF_ASSERT_CACHED_CONTROL_STATE(cleave_state);

static tf_ret cleave_step(tf_ctx *ctx, void *state, bool *done) {
    cleave_state *s = state;

    if (s->awaiting_branch) {
        tf_ret res = collect_outputs(ctx, s->stack.len, s->outputs);
        if (res != TF_OK) return res;
        s->awaiting_branch = false;
        s->index++;
    }

    if (s->index >= s->branches->vector.len) {
        stack_snapshot_restore(ctx, &s->stack);
        if (s->construct_result) {
            tf_stack_push(ctx, s->outputs);
            s->outputs = NULL;
        } else {
            for (size_t i = 0; i < s->outputs->vector.len; i++) {
                tf_stack_push(ctx, s->outputs->vector.elem[i]);
                tf_obj_retain(s->outputs->vector.elem[i]);
            }
        }
        *done = true;
        return TF_OK;
    }

    tf_obj *branch = s->branches->vector.elem[s->index];
    if (!tf_obj_is_callable(branch)) return TF_ERR;

    stack_snapshot_restore(ctx, &s->stack);
    tf_stack_push(ctx, s->value);
    tf_obj_retain(s->value);
    s->awaiting_branch = true;
    *done = false;
    return tf_vm_call_callable(ctx, branch);
}

static void cleave_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    cleave_state *s = state;
    (void)status;
    stack_snapshot_release(ctx, &s->stack, s->inline_stack);
    tf_obj_release(s->branches);
    tf_obj_release(s->value);
    if (s->outputs) tf_obj_release(s->outputs);
    control_state_release(s, sizeof(*s));
}

typedef enum {
    TF_BI_RUN_LEFT,
    TF_BI_AFTER_LEFT,
    TF_BI_RUN_RIGHT,
    TF_BI_AFTER_RIGHT
} bi_stage;

typedef struct {
    tf_obj *left;
    tf_obj *right;
    tf_obj *saved;
    size_t base_len;
    bi_stage stage;
    tf_obj **left_outputs;
    size_t left_out_len;
} bi_state;

TF_ASSERT_CACHED_CONTROL_STATE(bi_state);

static tf_ret bi_step(tf_ctx *ctx, void *state, bool *done) {
    bi_state *s = state;

    if (s->stage == TF_BI_RUN_LEFT) {
        tf_obj_retain(s->saved);
        tf_stack_push(ctx, s->saved);
        s->stage = TF_BI_AFTER_LEFT;
        *done = false;
        return tf_vm_call_callable(ctx, s->left);
    }

    if (s->stage == TF_BI_AFTER_LEFT) {
        if (tf_stack_len(ctx) < s->base_len) return TF_ERR;
        s->left_out_len = tf_stack_len(ctx) - s->base_len;
        s->left_outputs = s->left_out_len > 0
                              ? tf_xmalloc(sizeof(tf_obj *) * s->left_out_len)
                              : NULL;
        for (size_t i = s->left_out_len; i > 0; i--) {
            s->left_outputs[i - 1] = tf_stack_pop(ctx);
        }

        tf_obj_retain(s->saved);
        tf_stack_push(ctx, s->saved);
        s->stage = TF_BI_AFTER_RIGHT;
        *done = false;
        return tf_vm_call_callable(ctx, s->right);
    }

    if (tf_stack_len(ctx) < s->base_len) return TF_ERR;
    size_t right_out_len = tf_stack_len(ctx) - s->base_len;
    tf_obj **right_outputs =
        right_out_len > 0 ? tf_xmalloc(sizeof(tf_obj *) * right_out_len) : NULL;
    for (size_t i = right_out_len; i > 0; i--) {
        right_outputs[i - 1] = tf_stack_pop(ctx);
    }

    for (size_t i = 0; i < s->left_out_len; i++) {
        tf_stack_push(ctx, s->left_outputs[i]);
    }
    free(s->left_outputs);
    s->left_outputs = NULL;
    s->left_out_len = 0;

    for (size_t i = 0; i < right_out_len; i++) tf_stack_push(ctx, right_outputs[i]);
    free(right_outputs);

    *done = true;
    return TF_OK;
}

static void bi_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    (void)ctx;
    (void)status;
    bi_state *s = state;
    for (size_t i = 0; i < s->left_out_len; i++) {
        tf_obj_release(s->left_outputs[i]);
    }
    free(s->left_outputs);
    tf_obj_release(s->left);
    tf_obj_release(s->right);
    if (s->saved) tf_obj_release(s->saved);
    control_state_release(s, sizeof(*s));
}

typedef enum { TF_TRY_START, TF_TRY_BODY, TF_TRY_HANDLER } try_stage;

typedef struct {
    tf_obj *body;
    tf_obj *handler;
    stack_snapshot stack;
    try_stage stage;
    bool suppressing_errors;
    tf_obj *inline_stack[TF_STACK_SNAPSHOT_INLINE];
} try_state;

TF_ASSERT_CACHED_CONTROL_STATE(try_state);

static tf_ret try_step(tf_ctx *ctx, void *state, bool *done) {
    try_state *s = state;
    if (s->stage == TF_TRY_START) {
        ctx->error_suppression_depth++;
        s->suppressing_errors = true;
        s->stage = TF_TRY_BODY;
        *done = false;
        return tf_vm_call_callable(ctx, s->body);
    }

    if (s->stage == TF_TRY_BODY) {
        if (s->suppressing_errors) {
            ctx->error_suppression_depth--;
            s->suppressing_errors = false;
        }
        *done = true;
        return TF_OK;
    }

    *done = true;
    return TF_OK;
}

static tf_ret try_error(tf_ctx *ctx, void *state, tf_ret status, bool *handled) {
    try_state *s = state;
    *handled = false;
    if (status != TF_ERR || s->stage != TF_TRY_BODY) return TF_OK;

    if (s->suppressing_errors) {
        ctx->error_suppression_depth--;
        s->suppressing_errors = false;
    }
    const char *message = tf_ctx_last_error(ctx);
    tf_obj *error = tf_obj_new_string(message ? message : "execution failed",
                                      message ? strlen(message) : 16);
    stack_snapshot_restore(ctx, &s->stack);
    tf_stack_push(ctx, error);
    tf_ctx_clear_error(ctx);
    ctx->error_reported = false;
    ctx->program_error = false;
    s->stage = TF_TRY_HANDLER;

    tf_ret res = tf_vm_call_callable(ctx, s->handler);
    if (res == TF_OK) *handled = true;
    return res;
}

static void try_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    try_state *s = state;
    (void)status;
    if (s->suppressing_errors) {
        ctx->error_suppression_depth--;
        s->suppressing_errors = false;
    }
    stack_snapshot_release(ctx, &s->stack, s->inline_stack);
    tf_obj_release(s->body);
    tf_obj_release(s->handler);
    control_state_release(s, sizeof(*s));
}

typedef enum { TF_PRED_IDLE, TF_PRED_AWAITING } predicate_eval_phase;

#define TF_PREDICATE_INLINE_STACK 16

typedef struct {
    predicate_eval_phase phase;
    tf_obj **saved_stack;
    size_t saved_len;
    bool reusable_snapshot;
    tf_obj *inline_stack[TF_PREDICATE_INLINE_STACK];
} predicate_eval;

static void predicate_save_stack(tf_ctx *ctx, predicate_eval *eval) {
    eval->saved_len = tf_stack_len(ctx);
    if (eval->saved_len == 0) {
        eval->saved_stack = NULL;
    } else if (eval->saved_len <= TF_PREDICATE_INLINE_STACK) {
        eval->saved_stack = eval->inline_stack;
    } else {
        eval->saved_stack =
            tf_scratch_alloc(ctx, sizeof(tf_obj *) * eval->saved_len);
    }
    for (size_t i = 0; i < eval->saved_len; i++) {
        eval->saved_stack[i] =
            tf_stack_peek(ctx, eval->saved_len - 1 - i);
        tf_obj_retain(eval->saved_stack[i]);
    }
}

static void predicate_free_stack_storage(tf_ctx *ctx, predicate_eval *eval) {
    if (eval->saved_stack && eval->saved_stack != eval->inline_stack) {
        tf_scratch_release(ctx, eval->saved_stack);
    }
    eval->saved_stack = NULL;
    eval->saved_len = 0;
}

static void predicate_release_stack(tf_ctx *ctx, predicate_eval *eval) {
    for (size_t i = 0; i < eval->saved_len; i++) {
        tf_obj_release(eval->saved_stack[i]);
    }
    predicate_free_stack_storage(ctx, eval);
}

static void predicate_eval_init(predicate_eval *eval) {
    eval->phase = TF_PRED_IDLE;
    eval->saved_stack = NULL;
    eval->saved_len = 0;
    eval->reusable_snapshot = false;
}

static void predicate_eval_init_reusable(tf_ctx *ctx, predicate_eval *eval) {
    predicate_eval_init(eval);
    predicate_save_stack(ctx, eval);
    eval->reusable_snapshot = true;
}

static tf_ret predicate_eval_step(tf_ctx *ctx, predicate_eval *eval, tf_obj *pred,
                                  bool allow_bool, tf_obj **inputs, size_t input_len,
                                  bool *ready, bool *pred_val) {
    *ready = false;

    if (eval->phase == TF_PRED_IDLE) {
        if (tf_obj_typeof(pred) == TF_OBJ_TYPE_BOOL) {
            if (!allow_bool || input_len != 0) return TF_ERR;
            *pred_val = pred->b;
            *ready = true;
            return TF_OK;
        }

        if (!tf_obj_is_callable(pred)) return TF_ERR;

        if (!eval->reusable_snapshot) {
            predicate_save_stack(ctx, eval);
        }
        for (size_t i = 0; i < input_len; i++) {
            tf_stack_push(ctx, inputs[i]);
            tf_obj_retain(inputs[i]);
        }

        eval->phase = TF_PRED_AWAITING;
        return tf_vm_call_callable(ctx, pred);
    }

    tf_obj *bool_res = tf_stack_pop_type(ctx, TF_OBJ_TYPE_BOOL);
    if (!bool_res) return TF_ERR;

    *pred_val = bool_res->b;
    tf_obj_release(bool_res);

    if (eval->reusable_snapshot) {
        restore_stack_copy(ctx, eval->saved_stack, eval->saved_len);
    } else {
        restore_stack_owned(ctx, eval->saved_stack, eval->saved_len);
        predicate_free_stack_storage(ctx, eval);
    }
    eval->phase = TF_PRED_IDLE;
    *ready = true;
    return TF_OK;
}

static void predicate_eval_cleanup(tf_ctx *ctx, predicate_eval *eval) {
    predicate_release_stack(ctx, eval);
    predicate_eval_init(eval);
}

typedef enum { TF_IF_COND, TF_IF_BODY } if_stage;

typedef struct {
    tf_obj *cond;
    tf_obj *then_b;
    tf_obj *else_b;
    bool has_else;
    if_stage stage;
    predicate_eval pred_eval;
} if_state;

static tf_ret if_step(tf_ctx *ctx, void *state, bool *done) {
    if_state *s = state;
    if (s->stage == TF_IF_COND) {
        bool ready = false;
        bool cond_val = false;
        tf_ret res = predicate_eval_step(ctx, &s->pred_eval, s->cond, true, NULL, 0,
                                         &ready, &cond_val);
        if (res != TF_OK || !ready) {
            *done = false;
            return res;
        }

        tf_obj *body = cond_val ? s->then_b : s->else_b;
        if (!body) {
            *done = true;
            return TF_OK;
        }
        s->stage = TF_IF_BODY;
        *done = false;
        return tf_vm_call_callable(ctx, body);
    }

    *done = true;
    return TF_OK;
}

static void if_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    (void)status;
    if_state *s = state;
    predicate_eval_cleanup(ctx, &s->pred_eval);
    tf_obj_release(s->cond);
    tf_obj_release(s->then_b);
    if (s->has_else) tf_obj_release(s->else_b);
    control_state_release(s, sizeof(*s));
}

typedef enum { TF_WHILE_COND, TF_WHILE_BODY } while_stage;

typedef struct {
    tf_obj *cond;
    tf_obj *body;
    while_stage stage;
    predicate_eval pred_eval;
} while_state;

static tf_ret while_step(tf_ctx *ctx, void *state, bool *done) {
    while_state *s = state;
    if (s->stage == TF_WHILE_COND) {
        bool ready = false;
        bool continue_loop = false;
        tf_ret res = predicate_eval_step(ctx, &s->pred_eval, s->cond, false, NULL, 0,
                                         &ready, &continue_loop);
        if (res != TF_OK || !ready) {
            *done = false;
            return res;
        }
        if (!continue_loop) {
            *done = true;
            return TF_OK;
        }
        s->stage = TF_WHILE_BODY;
        *done = false;
        return tf_vm_call_callable(ctx, s->body);
    }

    s->stage = TF_WHILE_COND;
    *done = false;
    return TF_OK;
}

static void while_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    (void)status;
    while_state *s = state;
    predicate_eval_cleanup(ctx, &s->pred_eval);
    tf_obj_release(s->cond);
    tf_obj_release(s->body);
    control_state_release(s, sizeof(*s));
}

typedef enum { TF_COND_PRED, TF_COND_BODY } cond_stage;

typedef struct {
    tf_obj *clauses;
    size_t index;
    cond_stage stage;
    predicate_eval pred_eval;
} cond_state;

static tf_ret cond_step(tf_ctx *ctx, void *state, bool *done) {
    cond_state *s = state;
    if (s->stage == TF_COND_BODY) {
        *done = true;
        return TF_OK;
    }

    while (s->index < s->clauses->vector.len) {
        tf_obj *clause = s->clauses->vector.elem[s->index];
        if (tf_obj_typeof(clause) != TF_OBJ_TYPE_VECTOR ||
            clause->vector.len != 2) {
            return TF_ERR;
        }

        tf_obj *pred = clause->vector.elem[0];
        tf_obj *body = clause->vector.elem[1];
        if (!tf_obj_is_callable(body)) return TF_ERR;

        bool ready = false;
        bool cond_val = false;
        tf_ret res = predicate_eval_step(ctx, &s->pred_eval, pred, true, NULL, 0,
                                         &ready, &cond_val);
        if (res != TF_OK || !ready) {
            *done = false;
            return res;
        }

        if (cond_val) {
            s->stage = TF_COND_BODY;
            /* Predicates share one observer snapshot, but the selected body
             * threads the restored stack and must not keep that snapshot
             * alive across copy-on-write updates. */
            predicate_eval_cleanup(ctx, &s->pred_eval);
            *done = false;
            return tf_vm_call_callable(ctx, body);
        }

        s->index++;
    }

    *done = true;
    return TF_OK;
}

static void cond_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    (void)status;
    cond_state *s = state;
    predicate_eval_cleanup(ctx, &s->pred_eval);
    tf_obj_release(s->clauses);
    control_state_release(s, sizeof(*s));
}

typedef struct {
    tf_obj *pred;
    tf_obj *seq;
    tf_sequence_iter iter;
    tf_obj *current;
    bool string_result;
    bool list_result;
    tf_obj *vector_result;
    tf_list_builder list_builder;
    bytebuf str_result;
    predicate_eval pred_eval;
} filter_state;

static tf_ret filter_step(tf_ctx *ctx, void *state, bool *done) {
    filter_state *s = state;

    while (true) {
        if (!s->current) {
            s->current = tf_sequence_iter_next_owned(&s->iter);
        }
        if (!s->current) break;

        tf_obj *inputs[] = {s->current};
        bool ready = false;
        bool keep = false;
        tf_ret res = predicate_eval_step(ctx, &s->pred_eval, s->pred, false, inputs,
                                         1, &ready, &keep);
        if (res != TF_OK || !ready) {
            *done = false;
            return res;
        }

        if (keep) {
            if (s->string_result) {
                bytebuf_append(&s->str_result, s->current->str.ptr[0]);
                tf_obj_release(s->current);
            } else if (s->list_result) {
                tf_list_builder_push_owned(&s->list_builder, s->current);
            } else {
                /* Keep tiny sparse results inline and cap speculative reserve. */
                if (s->vector_result->vector.len == TF_VECTOR_INLINE_CAP) {
                    size_t reserve = sequence_len(s->seq);
                    if (reserve > TF_FILTER_VECTOR_RESERVE_CAP) {
                        reserve = TF_FILTER_VECTOR_RESERVE_CAP;
                    }
                    tf_vector_reserve(s->vector_result, reserve);
                }
                tf_vector_push(s->vector_result, s->current);
            }
        } else {
            tf_obj_release(s->current);
        }
        s->current = NULL;
    }

    if (s->string_result) {
        tf_stack_push(ctx, bytebuf_to_string(&s->str_result));
        free(s->str_result.ptr);
        s->str_result.ptr = NULL;
    } else if (s->list_result) {
        tf_stack_push(ctx, tf_list_builder_finish(&s->list_builder));
    } else {
        tf_stack_push(ctx, s->vector_result);
        s->vector_result = NULL;
    }
    *done = true;
    return TF_OK;
}

static void filter_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    (void)status;
    filter_state *s = state;
    predicate_eval_cleanup(ctx, &s->pred_eval);
    tf_obj_release(s->pred);
    tf_obj_release(s->seq);
    if (s->current) tf_obj_release(s->current);
    if (s->string_result) {
        free(s->str_result.ptr);
    } else if (s->list_result) {
        tf_list_builder_cleanup(&s->list_builder);
    } else if (s->vector_result) {
        tf_obj_release(s->vector_result);
    }
    control_state_release(s, sizeof(*s));
}

typedef enum { TF_QUANT_SOME, TF_QUANT_ALL } quantifier_kind;

typedef struct {
    tf_obj *pred;
    tf_obj *seq;
    tf_sequence_iter iter;
    tf_obj *current;
    quantifier_kind kind;
    predicate_eval pred_eval;
} quantifier_state;

static tf_ret quantifier_step(tf_ctx *ctx, void *state, bool *done) {
    quantifier_state *s = state;

    while (true) {
        if (!s->current) {
            s->current = tf_sequence_iter_next_owned(&s->iter);
        }
        if (!s->current) break;

        tf_obj *inputs[] = {s->current};
        bool ready = false;
        bool pred_val = false;
        tf_ret res = predicate_eval_step(ctx, &s->pred_eval, s->pred, false, inputs,
                                         1, &ready, &pred_val);
        if (res != TF_OK || !ready) {
            *done = false;
            return res;
        }

        tf_obj_release(s->current);
        s->current = NULL;

        if (s->kind == TF_QUANT_SOME && pred_val) {
            tf_stack_push(ctx, tf_obj_new_bool(true));
            *done = true;
            return TF_OK;
        }
        if (s->kind == TF_QUANT_ALL && !pred_val) {
            tf_stack_push(ctx, tf_obj_new_bool(false));
            *done = true;
            return TF_OK;
        }
    }

    tf_stack_push(ctx, tf_obj_new_bool(s->kind == TF_QUANT_ALL));
    *done = true;
    return TF_OK;
}

static void quantifier_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    (void)status;
    quantifier_state *s = state;
    predicate_eval_cleanup(ctx, &s->pred_eval);
    tf_obj_release(s->pred);
    tf_obj_release(s->seq);
    if (s->current) tf_obj_release(s->current);
    control_state_release(s, sizeof(*s));
}

typedef struct {
    tf_obj *pred;
    tf_obj *seq;
    tf_sequence_iter iter;
    tf_obj *current;
    bool string_result;
    bool list_result;
    tf_obj *true_vector;
    tf_obj *false_vector;
    tf_list_builder true_builder;
    tf_list_builder false_builder;
    bytebuf true_str;
    bytebuf false_str;
    predicate_eval pred_eval;
} split_state;

static tf_ret split_step(tf_ctx *ctx, void *state, bool *done) {
    split_state *s = state;

    while (true) {
        if (!s->current) {
            s->current = tf_sequence_iter_next_owned(&s->iter);
        }
        if (!s->current) break;

        tf_obj *inputs[] = {s->current};
        bool ready = false;
        bool pred_val = false;
        tf_ret res = predicate_eval_step(ctx, &s->pred_eval, s->pred, false, inputs,
                                         1, &ready, &pred_val);
        if (res != TF_OK || !ready) {
            *done = false;
            return res;
        }

        if (s->string_result) {
            bytebuf_append(pred_val ? &s->true_str : &s->false_str,
                           s->current->str.ptr[0]);
            tf_obj_release(s->current);
        } else if (s->list_result) {
            tf_list_builder_push_owned(
                pred_val ? &s->true_builder : &s->false_builder, s->current);
        } else {
            tf_vector_push(pred_val ? s->true_vector : s->false_vector,
                           s->current);
        }
        s->current = NULL;
    }

    if (s->string_result) {
        tf_stack_push(ctx, bytebuf_to_string(&s->true_str));
        tf_stack_push(ctx, bytebuf_to_string(&s->false_str));
        free(s->true_str.ptr);
        free(s->false_str.ptr);
        s->true_str.ptr = NULL;
        s->false_str.ptr = NULL;
    } else if (s->list_result) {
        tf_stack_push(ctx, tf_list_builder_finish(&s->true_builder));
        tf_stack_push(ctx, tf_list_builder_finish(&s->false_builder));
    } else {
        tf_stack_push(ctx, s->true_vector);
        tf_stack_push(ctx, s->false_vector);
        s->true_vector = NULL;
        s->false_vector = NULL;
    }
    *done = true;
    return TF_OK;
}

static void split_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    (void)status;
    split_state *s = state;
    predicate_eval_cleanup(ctx, &s->pred_eval);
    tf_obj_release(s->pred);
    tf_obj_release(s->seq);
    if (s->current) tf_obj_release(s->current);
    if (s->string_result) {
        free(s->true_str.ptr);
        free(s->false_str.ptr);
    } else if (s->list_result) {
        tf_list_builder_cleanup(&s->true_builder);
        tf_list_builder_cleanup(&s->false_builder);
    } else {
        if (s->true_vector) tf_obj_release(s->true_vector);
        if (s->false_vector) tf_obj_release(s->false_vector);
    }
    control_state_release(s, sizeof(*s));
}

typedef struct {
    tf_obj *pred;
    tf_obj *l1;
    tf_obj *l2;
    tf_sequence_iter iter1;
    tf_sequence_iter iter2;
    tf_obj *o1;
    tf_obj *o2;
    bool string_result;
    bool list_result;
    tf_obj *vector_result;
    tf_list_builder list_builder;
    bytebuf str_result;
    predicate_eval pred_eval;
} merge_state;

static void merge_append_owned(merge_state *s, tf_obj *item) {
    if (s->string_result) {
        bytebuf_append(&s->str_result, item->str.ptr[0]);
        tf_obj_release(item);
    } else if (s->list_result) {
        tf_list_builder_push_owned(&s->list_builder, item);
    } else {
        tf_vector_push(s->vector_result, item);
    }
}

static void merge_take_left(merge_state *s) {
    merge_append_owned(s, s->o1);
    s->o1 = NULL;
}

static void merge_take_right(merge_state *s) {
    merge_append_owned(s, s->o2);
    s->o2 = NULL;
}

static tf_ret merge_step(tf_ctx *ctx, void *state, bool *done) {
    merge_state *s = state;

    while (true) {
        if (!s->o1) s->o1 = tf_sequence_iter_next_owned(&s->iter1);
        if (!s->o2) s->o2 = tf_sequence_iter_next_owned(&s->iter2);
        if (!s->o1 || !s->o2) break;

        tf_obj *inputs[] = {s->o1, s->o2};
        bool ready = false;
        bool take_left = false;
        tf_ret res = predicate_eval_step(ctx, &s->pred_eval, s->pred, false, inputs,
                                         2, &ready, &take_left);
        if (res != TF_OK || !ready) {
            *done = false;
            return res;
        }

        if (take_left) {
            merge_take_left(s);
        } else {
            merge_take_right(s);
        }
    }

    if (s->o1) {
        merge_append_owned(s, s->o1);
        s->o1 = NULL;
    }
    while (true) {
        tf_obj *item = tf_sequence_iter_next_owned(&s->iter1);
        if (!item) break;
        merge_append_owned(s, item);
    }
    if (s->o2) {
        merge_append_owned(s, s->o2);
        s->o2 = NULL;
    }
    while (true) {
        tf_obj *item = tf_sequence_iter_next_owned(&s->iter2);
        if (!item) break;
        merge_append_owned(s, item);
    }

    if (s->string_result) {
        tf_stack_push(ctx, bytebuf_to_string(&s->str_result));
        free(s->str_result.ptr);
        s->str_result.ptr = NULL;
    } else if (s->list_result) {
        tf_stack_push(ctx, tf_list_builder_finish(&s->list_builder));
    } else {
        tf_stack_push(ctx, s->vector_result);
        s->vector_result = NULL;
    }
    *done = true;
    return TF_OK;
}

static void merge_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    (void)status;
    merge_state *s = state;
    predicate_eval_cleanup(ctx, &s->pred_eval);
    tf_obj_release(s->pred);
    tf_obj_release(s->l1);
    tf_obj_release(s->l2);
    if (s->o1) tf_obj_release(s->o1);
    if (s->o2) tf_obj_release(s->o2);
    if (s->string_result) {
        free(s->str_result.ptr);
    } else if (s->list_result) {
        tf_list_builder_cleanup(&s->list_builder);
    } else if (s->vector_result) {
        tf_obj_release(s->vector_result);
    }
    control_state_release(s, sizeof(*s));
}

typedef enum {
    TF_LINEAR_REC_PRED,
    TF_LINEAR_REC_THEN,
    TF_LINEAR_REC_BEFORE,
    TF_LINEAR_REC_AFTER
} linear_rec_stage;

/* Linrec and genrec need only the number of deferred after-callables while
 * descending. One continuation therefore replaces a native frame per level. */
typedef struct {
    tf_obj *pred;
    tf_obj *then_b;
    tf_obj *before;
    tf_obj *after;
    size_t pending_after;
    linear_rec_stage stage;
    predicate_eval pred_eval;
} linear_rec_state;

TF_ASSERT_CACHED_CONTROL_STATE(linear_rec_state);

static tf_ret linear_rec_step(tf_ctx *ctx, void *state, bool *done) {
    linear_rec_state *s = state;
    *done = false;

    while (true) {
        if (s->stage == TF_LINEAR_REC_PRED) {
            bool ready = false;
            bool is_done = false;
            tf_ret res = predicate_eval_step(ctx, &s->pred_eval, s->pred,
                                             false, NULL, 0, &ready,
                                             &is_done);
            if (res != TF_OK || !ready) return res;

            s->stage = is_done ? TF_LINEAR_REC_THEN
                               : TF_LINEAR_REC_BEFORE;
            return tf_vm_call_callable(ctx,
                                       is_done ? s->then_b : s->before);
        }

        if (s->stage == TF_LINEAR_REC_BEFORE) {
            s->pending_after++;
            s->stage = TF_LINEAR_REC_PRED;
            continue;
        }

        if (s->pending_after == 0) {
            *done = true;
            return TF_OK;
        }

        s->pending_after--;
        s->stage = TF_LINEAR_REC_AFTER;
        return tf_vm_call_callable(ctx, s->after);
    }
}

static void linear_rec_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    (void)status;
    linear_rec_state *s = state;
    predicate_eval_cleanup(ctx, &s->pred_eval);
    tf_obj_release(s->pred);
    tf_obj_release(s->then_b);
    tf_obj_release(s->before);
    tf_obj_release(s->after);
    control_state_release(s, sizeof(*s));
}

static tf_ret linear_rec_start(tf_ctx *ctx, tf_obj *pred, tf_obj *then_b,
                               tf_obj *before, tf_obj *after) {
    linear_rec_state *state = control_state_acquire(sizeof(*state));
    state->pred = pred;
    state->then_b = then_b;
    state->before = before;
    state->after = after;
    state->pending_after = 0;
    state->stage = TF_LINEAR_REC_PRED;
    predicate_eval_init(&state->pred_eval);
    tf_frame_push_native(ctx, linear_rec_step, linear_rec_cleanup, state);
    return TF_OK;
}

typedef enum {
    TF_BINREC_PRED,
    TF_BINREC_THEN,
    TF_BINREC_REC1,
    TF_BINREC_AFTER_FIRST,
    TF_BINREC_AFTER_SECOND,
    TF_BINREC_COMBINE
} binrec_stage;

#define TF_BINREC_INLINE_LEVELS 6

/* A binary recursion controller keeps the shared callables and predicate
 * controller once. Each logical level owns only its hidden second branch. */
typedef struct {
    tf_obj *hidden;
    binrec_stage stage;
} binrec_level;

typedef struct {
    tf_obj *pred;
    tf_obj *then_b;
    tf_obj *rec1;
    tf_obj *rec2;
    predicate_eval pred_eval;
    binrec_level *levels;
    size_t levels_len;
    size_t levels_cap;
    binrec_level inline_levels[TF_BINREC_INLINE_LEVELS];
} binrec_state;

TF_ASSERT_CACHED_CONTROL_STATE(binrec_state);

static void binrec_push_level(binrec_state *state) {
    if (state->levels_len == state->levels_cap) {
        size_t new_cap = state->levels_cap * 2;
        if (state->levels == state->inline_levels) {
            binrec_level *levels =
                tf_xmalloc(sizeof(binrec_level) * new_cap);
            memcpy(levels, state->inline_levels,
                   sizeof(binrec_level) * state->levels_len);
            state->levels = levels;
        } else {
            state->levels =
                tf_xrealloc(state->levels, sizeof(binrec_level) * new_cap);
        }
        state->levels_cap = new_cap;
    }

    binrec_level *level = &state->levels[state->levels_len++];
    level->hidden = NULL;
    level->stage = TF_BINREC_PRED;
}

static tf_ret binrec_step(tf_ctx *ctx, void *state, bool *done) {
    binrec_state *s = state;
    *done = false;

    while (s->levels_len > 0) {
        binrec_level *level = &s->levels[s->levels_len - 1];
        if (level->stage == TF_BINREC_PRED) {
            bool ready = false;
            bool is_done = false;
            tf_ret res = predicate_eval_step(ctx, &s->pred_eval, s->pred,
                                             false, NULL, 0, &ready,
                                             &is_done);
            if (res != TF_OK || !ready) return res;

            level->stage = is_done ? TF_BINREC_THEN : TF_BINREC_REC1;
            return tf_vm_call_callable(ctx,
                                       is_done ? s->then_b : s->rec1);
        }

        if (level->stage == TF_BINREC_THEN ||
            level->stage == TF_BINREC_COMBINE) {
            s->levels_len--;
            if (s->levels_len == 0) {
                *done = true;
                return TF_OK;
            }
            continue;
        }

        if (level->stage == TF_BINREC_REC1) {
            if (!tf_ctx_require_stack(ctx, 1)) return TF_ERR;
            level->hidden = tf_stack_pop(ctx);
            level->stage = TF_BINREC_AFTER_FIRST;
            binrec_push_level(s);
            continue;
        }

        if (level->stage == TF_BINREC_AFTER_FIRST) {
            tf_stack_push(ctx, level->hidden);
            level->hidden = NULL;
            level->stage = TF_BINREC_AFTER_SECOND;
            binrec_push_level(s);
            continue;
        }

        level->stage = TF_BINREC_COMBINE;
        return tf_vm_call_callable(ctx, s->rec2);
    }

    *done = true;
    return TF_OK;
}

static void binrec_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    binrec_state *s = state;
    (void)status;
    predicate_eval_cleanup(ctx, &s->pred_eval);

    while (s->levels_len > 0) {
        binrec_level *level = &s->levels[s->levels_len - 1];
        if (level->hidden) tf_obj_release(level->hidden);
        s->levels_len--;
    }

    if (s->levels != s->inline_levels) free(s->levels);
    tf_obj_release(s->pred);
    tf_obj_release(s->then_b);
    tf_obj_release(s->rec1);
    tf_obj_release(s->rec2);
    control_state_release(s, sizeof(*s));
}

typedef enum {
    TF_TREEREC_START,
    TF_TREEREC_LEAF_BODY,
    TF_TREEREC_CHILD_LOOP,
    TF_TREEREC_CHILD_DONE,
    TF_TREEREC_NODE_BODY
} treerec_stage;

typedef struct {
    tf_obj *tree;
    tf_obj *leaf;
    tf_obj *node;
    tf_obj *mapped;
    size_t index;
    stack_snapshot stack;
    treerec_stage stage;
    tf_obj *inline_stack[TF_STACK_SNAPSHOT_INLINE];
} treerec_state;

TF_ASSERT_CACHED_CONTROL_STATE(treerec_state);

static void treerec_push_owned(tf_ctx *ctx, tf_obj *tree, tf_obj *leaf,
                               tf_obj *node);

static tf_ret treerec_finish_single(tf_ctx *ctx, treerec_state *s, bool *done) {
    if (tf_stack_len(ctx) != s->stack.len + 1) return TF_ERR;
    tf_obj *result = tf_stack_pop(ctx);
    stack_snapshot_restore(ctx, &s->stack);
    tf_stack_push(ctx, result);
    *done = true;
    return TF_OK;
}

static tf_ret treerec_step(tf_ctx *ctx, void *state, bool *done) {
    treerec_state *s = state;

    if (s->stage == TF_TREEREC_START) {
        if (tf_obj_typeof(s->tree) != TF_OBJ_TYPE_VECTOR) {
            tf_stack_push(ctx, s->tree);
            tf_obj_retain(s->tree);
            s->stage = TF_TREEREC_LEAF_BODY;
            *done = false;
            return tf_vm_call_callable(ctx, s->leaf);
        }

        s->mapped =
            tf_obj_new_vector_with_capacity(s->tree->vector.len);
        s->index = 0;
        s->stage = TF_TREEREC_CHILD_LOOP;
    }

    if (s->stage == TF_TREEREC_LEAF_BODY) {
        return treerec_finish_single(ctx, s, done);
    }

    if (s->stage == TF_TREEREC_CHILD_DONE) {
        if (tf_stack_len(ctx) != s->stack.len + 1) return TF_ERR;
        tf_obj *child_result = tf_stack_pop(ctx);
        tf_vector_push(s->mapped, child_result);
        stack_snapshot_restore(ctx, &s->stack);
        s->index++;
        s->stage = TF_TREEREC_CHILD_LOOP;
        *done = false;
        return TF_OK;
    }

    if (s->stage == TF_TREEREC_CHILD_LOOP) {
        if (s->index < s->tree->vector.len) {
            tf_obj *child = s->tree->vector.elem[s->index];
            tf_obj_retain(child);
            tf_obj_retain(s->leaf);
            tf_obj_retain(s->node);
            treerec_push_owned(ctx, child, s->leaf, s->node);
            s->stage = TF_TREEREC_CHILD_DONE;
            *done = false;
            return TF_OK;
        }

        tf_stack_push(ctx, s->mapped);
        tf_obj_retain(s->mapped);
        s->stage = TF_TREEREC_NODE_BODY;
        *done = false;
        return tf_vm_call_callable(ctx, s->node);
    }

    return treerec_finish_single(ctx, s, done);
}

static void treerec_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    treerec_state *s = state;
    (void)status;
    stack_snapshot_release(ctx, &s->stack, s->inline_stack);
    tf_obj_release(s->tree);
    tf_obj_release(s->leaf);
    tf_obj_release(s->node);
    if (s->mapped) tf_obj_release(s->mapped);
    control_state_release(s, sizeof(*s));
}

static void treerec_push_owned(tf_ctx *ctx, tf_obj *tree, tf_obj *leaf,
                               tf_obj *node) {
    treerec_state *state = control_state_acquire(sizeof(*state));
    state->tree = tree;
    state->leaf = leaf;
    state->node = node;
    state->mapped = NULL;
    state->index = 0;
    state->stage = TF_TREEREC_START;
    stack_snapshot_save(ctx, &state->stack, state->inline_stack);
    tf_frame_push_native(ctx, treerec_step, treerec_cleanup, state);
}

tf_ret tf_error(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_STR)) return TF_ERR;
    tf_obj *msg = tf_stack_pop(ctx);
    tf_ctx_program_errorf(ctx, "%s\n", msg->str.ptr);
    tf_obj_release(msg);
    ctx->program_error = true;
    return TF_ERR;
}

tf_ret tf_try(tf_ctx *ctx) {
    if (!tf_ctx_require_callable(ctx, 1) || !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *handler = tf_stack_peek(ctx, 0);
    tf_obj *body = tf_stack_peek(ctx, 1);

    handler = tf_stack_pop(ctx);
    body = tf_stack_pop(ctx);

    try_state *state = control_state_acquire(sizeof(*state));
    state->body = body;
    state->handler = handler;
    state->stage = TF_TRY_START;
    state->suppressing_errors = false;
    stack_snapshot_save(ctx, &state->stack, state->inline_stack);

    tf_frame_push_native_handler(ctx, try_step, try_cleanup, try_error, state);
    return TF_OK;
}

tf_ret tf_if(tf_ctx *ctx) {
    if (!require_condition(ctx, 1) || !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *body = tf_stack_peek(ctx, 0);
    tf_obj *cond = tf_stack_peek(ctx, 1);

    body = tf_stack_pop(ctx);
    cond = tf_stack_pop(ctx);

    if_state *state = control_state_acquire(sizeof(*state));
    state->cond = cond;
    state->then_b = body;
    state->else_b = NULL;
    state->has_else = false;
    state->stage = TF_IF_COND;
    predicate_eval_init(&state->pred_eval);

    tf_frame_push_native(ctx, if_step, if_cleanup, state);
    return TF_OK;
}

tf_ret tf_infra(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_VECTOR) ||
        !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *body = tf_stack_peek(ctx, 0);
    tf_obj *data = tf_stack_peek(ctx, 1);

    body = tf_stack_pop(ctx);
    data = tf_stack_pop(ctx);

    infra_state *state = control_state_acquire(sizeof(*state));
    state->body = body;
    state->data = data;
    state->scheduled = false;
    state->result = NULL;
    stack_snapshot_save(ctx, &state->stack, state->inline_stack);

    clear_stack(ctx);
    for (size_t i = 0; i < data->vector.len; i++) {
        tf_stack_push(ctx, data->vector.elem[i]);
        tf_obj_retain(data->vector.elem[i]);
    }

    tf_frame_push_native(ctx, infra_step, infra_cleanup, state);
    return TF_OK;
}

tf_ret tf_cond(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_VECTOR)) return TF_ERR;
    tf_obj *clauses = tf_stack_pop_type(ctx, TF_OBJ_TYPE_VECTOR);

    cond_state *state = control_state_acquire(sizeof(*state));
    state->clauses = clauses;
    state->index = 0;
    state->stage = TF_COND_PRED;
    predicate_eval_init_reusable(ctx, &state->pred_eval);

    tf_frame_push_native(ctx, cond_step, cond_cleanup, state);
    return TF_OK;
}

static tf_ret cleave_or_construct(tf_ctx *ctx, bool construct_result) {
    if (!tf_ctx_require_stack(ctx, 2) ||
        !tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_VECTOR)) {
        return TF_ERR;
    }
    tf_obj *branches = tf_stack_peek(ctx, 0);
    if (!require_callable_vector(ctx, branches,
                                 construct_result ? "construct" : "cleave")) {
        return TF_ERR;
    }

    branches = tf_stack_pop(ctx);
    tf_obj *value = tf_stack_pop(ctx);

    cleave_state *state = control_state_acquire(sizeof(*state));
    state->branches = branches;
    state->value = value;
    state->outputs = tf_obj_new_vector();
    state->index = 0;
    state->awaiting_branch = false;
    state->construct_result = construct_result;
    stack_snapshot_save(ctx, &state->stack, state->inline_stack);

    tf_frame_push_native(ctx, cleave_step, cleave_cleanup, state);
    return TF_OK;
}

tf_ret tf_cleave(tf_ctx *ctx) {
    return cleave_or_construct(ctx, false);
}

tf_ret tf_construct(tf_ctx *ctx) {
    return cleave_or_construct(ctx, true);
}

tf_ret tf_ifelse(tf_ctx *ctx) {
    if (!require_condition(ctx, 2) || !tf_ctx_require_callable(ctx, 1) ||
        !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *else_b = tf_stack_peek(ctx, 0);
    tf_obj *then_b = tf_stack_peek(ctx, 1);
    tf_obj *cond = tf_stack_peek(ctx, 2);

    else_b = tf_stack_pop(ctx);
    then_b = tf_stack_pop(ctx);
    cond = tf_stack_pop(ctx);

    if_state *state = control_state_acquire(sizeof(*state));
    state->cond = cond;
    state->then_b = then_b;
    state->else_b = else_b;
    state->has_else = true;
    state->stage = TF_IF_COND;
    predicate_eval_init(&state->pred_eval);

    tf_frame_push_native(ctx, if_step, if_cleanup, state);
    return TF_OK;
}

tf_ret tf_while(tf_ctx *ctx) {
    if (!tf_ctx_require_callable(ctx, 1) || !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *body = tf_stack_peek(ctx, 0);
    tf_obj *cond = tf_stack_peek(ctx, 1);

    body = tf_stack_pop(ctx);
    cond = tf_stack_pop(ctx);

    while_state *state = control_state_acquire(sizeof(*state));
    state->cond = cond;
    state->body = body;
    state->stage = TF_WHILE_COND;
    predicate_eval_init(&state->pred_eval);

    tf_frame_push_native(ctx, while_step, while_cleanup, state);
    return TF_OK;
}

tf_ret tf_dip(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 2) || !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *body = tf_stack_peek(ctx, 0);

    body = tf_stack_pop(ctx);
    tf_obj *saved = tf_stack_pop(ctx);

    dip_state *state = control_state_acquire(sizeof(*state));
    state->body = body;
    state->saved = saved;
    state->scheduled = false;

    tf_frame_push_native(ctx, dip_step, dip_cleanup, state);
    return TF_OK;
}

tf_ret tf_keep(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 2) || !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *body = tf_stack_peek(ctx, 0);

    body = tf_stack_pop(ctx);
    tf_obj *saved = tf_stack_pop(ctx);

    keep_state *state = control_state_acquire(sizeof(*state));
    state->body = body;
    state->saved = saved;
    state->base_len = tf_stack_len(ctx);
    state->scheduled = false;

    tf_frame_push_native(ctx, keep_step, keep_cleanup, state);
    return TF_OK;
}

tf_ret tf_bi(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 3) || !tf_ctx_require_callable(ctx, 1) ||
        !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *right = tf_stack_peek(ctx, 0);
    tf_obj *left = tf_stack_peek(ctx, 1);

    right = tf_stack_pop(ctx);
    left = tf_stack_pop(ctx);
    tf_obj *saved = tf_stack_pop(ctx);

    bi_state *state = control_state_acquire(sizeof(*state));
    state->left = left;
    state->right = right;
    state->saved = saved;
    state->base_len = tf_stack_len(ctx);
    state->stage = TF_BI_RUN_LEFT;
    state->left_outputs = NULL;
    state->left_out_len = 0;
    tf_frame_push_native(ctx, bi_step, bi_cleanup, state);
    return TF_OK;
}

tf_ret tf_linrec(tf_ctx *ctx) {
    if (!tf_ctx_require_callable(ctx, 3) || !tf_ctx_require_callable(ctx, 2) ||
        !tf_ctx_require_callable(ctx, 1) || !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *rec2 = tf_stack_peek(ctx, 0);
    tf_obj *rec1 = tf_stack_peek(ctx, 1);
    tf_obj *then_b = tf_stack_peek(ctx, 2);
    tf_obj *pred = tf_stack_peek(ctx, 3);

    rec2 = tf_stack_pop(ctx);
    rec1 = tf_stack_pop(ctx);
    then_b = tf_stack_pop(ctx);
    pred = tf_stack_pop(ctx);

    return linear_rec_start(ctx, pred, then_b, rec1, rec2);
}

tf_ret tf_binrec(tf_ctx *ctx) {
    if (!tf_ctx_require_callable(ctx, 3) || !tf_ctx_require_callable(ctx, 2) ||
        !tf_ctx_require_callable(ctx, 1) || !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *rec2 = tf_stack_peek(ctx, 0);
    tf_obj *rec1 = tf_stack_peek(ctx, 1);
    tf_obj *then_b = tf_stack_peek(ctx, 2);
    tf_obj *pred = tf_stack_peek(ctx, 3);

    rec2 = tf_stack_pop(ctx);
    rec1 = tf_stack_pop(ctx);
    then_b = tf_stack_pop(ctx);
    pred = tf_stack_pop(ctx);

    binrec_state *state = control_state_acquire(sizeof(*state));
    state->pred = pred;
    state->then_b = then_b;
    state->rec1 = rec1;
    state->rec2 = rec2;
    predicate_eval_init(&state->pred_eval);
    state->levels = state->inline_levels;
    state->levels_len = 0;
    state->levels_cap = TF_BINREC_INLINE_LEVELS;
    binrec_push_level(state);

    tf_frame_push_native(ctx, binrec_step, binrec_cleanup, state);
    return TF_OK;
}

tf_ret tf_genrec(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 4) || !tf_ctx_require_callable(ctx, 3) ||
        !tf_ctx_require_callable(ctx, 2) || !tf_ctx_require_callable(ctx, 1) ||
        !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }

    tf_obj *after = tf_stack_pop(ctx);
    tf_obj *before = tf_stack_pop(ctx);
    tf_obj *then_b = tf_stack_pop(ctx);
    tf_obj *pred = tf_stack_pop(ctx);

    return linear_rec_start(ctx, pred, then_b, before, after);
}

tf_ret tf_treerec(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 3) || !tf_ctx_require_callable(ctx, 1) ||
        !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *node = tf_stack_peek(ctx, 0);
    tf_obj *leaf = tf_stack_peek(ctx, 1);

    node = tf_stack_pop(ctx);
    leaf = tf_stack_pop(ctx);
    tf_obj *tree = tf_stack_pop(ctx);

    treerec_push_owned(ctx, tree, leaf, node);
    return TF_OK;
}

tf_ret tf_replicate(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_INT) ||
        !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *body = tf_stack_peek(ctx, 0);
    tf_obj *n_obj = tf_stack_peek(ctx, 1);
    body = tf_stack_pop(ctx);
    n_obj = tf_stack_pop(ctx);

    int64_t n = tf_obj_int_value(n_obj);
    tf_obj_release(n_obj);

    if (n < 0) {
        tf_obj_release(body);
        tf_ctx_runtime_errorf(ctx, "'%s' count must be non-negative\n",
                              ctx->current_word);
        return TF_ERR;
    }
    if ((uint64_t)n > SIZE_MAX / sizeof(tf_obj *)) {
        tf_obj_release(body);
        tf_ctx_runtime_errorf(ctx, "'%s' result is too large\n",
                              ctx->current_word);
        return TF_ERR;
    }

    replicate_state *state = control_state_acquire(sizeof(*state));
    state->body = body;
    state->remaining = n;
    state->awaiting_body = false;
    state->result = tf_obj_new_vector_with_capacity((size_t)n);
    stack_snapshot_save(ctx, &state->stack, state->inline_stack);

    tf_frame_push_native(ctx, replicate_step, replicate_cleanup, state);
    return TF_OK;
}

tf_ret tf_times(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_INT) ||
        !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *body = tf_stack_peek(ctx, 0);
    tf_obj *n_obj = tf_stack_peek(ctx, 1);

    body = tf_stack_pop(ctx);
    n_obj = tf_stack_pop(ctx);

    int64_t n = tf_obj_int_value(n_obj);
    if (n < 0) {
        tf_obj_release(body);
        tf_obj_release(n_obj);
        tf_ctx_runtime_errorf(ctx, "'%s' count must be non-negative\n",
                              ctx->current_word);
        return TF_ERR;
    }

    tf_obj_release(n_obj);
    if (n == 0) {
        tf_obj_release(body);
        return TF_OK;
    }

    times_state *state = control_state_acquire(sizeof(*state));
    state->body = body;
    state->remaining = n;
    tf_frame_push_native(ctx, times_step, times_cleanup, state);
    return TF_OK;
}

tf_ret tf_each(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 1) || !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *body = tf_stack_peek(ctx, 0);
    tf_obj *data = tf_stack_peek(ctx, 1);

    body = tf_stack_pop(ctx);
    data = tf_stack_pop(ctx);

    each_state *state = control_state_acquire(sizeof(*state));
    state->body = body;
    state->data = data;
    tf_sequence_iter_init(&state->iter, data);
    tf_frame_push_native(ctx, each_step, each_cleanup, state);
    return TF_OK;
}

tf_ret tf_map(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 1) || !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *body = tf_stack_peek(ctx, 0);
    tf_obj *data = tf_stack_peek(ctx, 1);

    body = tf_stack_pop(ctx);
    data = tf_stack_pop(ctx);

    map_state *state = control_state_acquire(sizeof(*state));
    state->body = body;
    state->data = data;
    tf_sequence_iter_init(&state->iter, data);
    state->awaiting_body = false;
    state->string_result = data->type == TF_OBJ_TYPE_STR;
    state->list_result = data->type == TF_OBJ_TYPE_LIST;
    state->vector_result = state->string_result || state->list_result
                               ? NULL
                               : tf_obj_new_vector_with_capacity(
                                     sequence_len(data));
    tf_list_builder_init(&state->list_builder);
    state->str_result.ptr = NULL;
    state->str_result.len = 0;
    state->str_result.cap = 0;
    if (state->string_result) bytebuf_init(&state->str_result, data->str.len);
    stack_snapshot_save(ctx, &state->stack, state->inline_stack);

    tf_frame_push_native(ctx, map_step, map_cleanup, state);
    return TF_OK;
}

tf_ret tf_fold(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 3) || !tf_ctx_require_sequence(ctx, 1) ||
        !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *body = tf_stack_peek(ctx, 0);
    tf_obj *data = tf_stack_peek(ctx, 1);

    body = tf_stack_pop(ctx);
    data = tf_stack_pop(ctx);
    tf_obj *acc = tf_stack_pop(ctx);

    fold_state *state = control_state_acquire(sizeof(*state));
    state->body = body;
    state->data = data;
    tf_sequence_iter_init(&state->iter, data);
    state->awaiting_body = false;
    state->base_len = tf_stack_len(ctx);

    tf_stack_push(ctx, acc);
    tf_frame_push_native(ctx, fold_step, fold_cleanup, state);
    return TF_OK;
}

tf_ret tf_split(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 2)) return TF_ERR;
    tf_obj *pred = tf_stack_peek(ctx, 0);
    tf_obj *seq = tf_stack_peek(ctx, 1);

    if (tf_obj_typeof(pred) == TF_OBJ_TYPE_STR &&
        tf_obj_typeof(seq) == TF_OBJ_TYPE_STR) {
        return tf_split_string(ctx);
    }

    if (!tf_obj_is_callable(pred) || !is_sequence(seq)) {
        if (!is_sequence(seq)) {
            tf_ctx_runtime_errorf(
                ctx, "'%s' expected sequence at stack depth 1, found %s\n",
                ctx->current_word, tf_obj_type_name(seq));
        } else {
            tf_ctx_runtime_errorf(ctx,
                                  "'%s' expected callable or string separator at "
                                  "stack depth 0, found %s\n",
                                  ctx->current_word, tf_obj_type_name(pred));
        }
        return TF_ERR;
    }

    pred = tf_stack_pop(ctx);
    seq = tf_stack_pop(ctx);

    split_state *state = control_state_acquire(sizeof(*state));
    state->pred = pred;
    state->seq = seq;
    tf_sequence_iter_init(&state->iter, seq);
    state->current = NULL;
    state->string_result = seq->type == TF_OBJ_TYPE_STR;
    state->list_result = seq->type == TF_OBJ_TYPE_LIST;
    state->true_vector = state->string_result || state->list_result
                             ? NULL
                             : tf_obj_new_vector();
    state->false_vector = state->string_result || state->list_result
                              ? NULL
                              : tf_obj_new_vector();
    tf_list_builder_init(&state->true_builder);
    tf_list_builder_init(&state->false_builder);
    state->true_str.ptr = NULL;
    state->true_str.len = 0;
    state->true_str.cap = 0;
    state->false_str.ptr = NULL;
    state->false_str.len = 0;
    state->false_str.cap = 0;
    if (state->string_result) {
        bytebuf_init(&state->true_str, seq->str.len);
        bytebuf_init(&state->false_str, seq->str.len);
    }
    predicate_eval_init_reusable(ctx, &state->pred_eval);

    tf_frame_push_native(ctx, split_step, split_cleanup, state);
    return TF_OK;
}

tf_ret tf_merge(tf_ctx *ctx) {
    if (!tf_ctx_require_stack(ctx, 3) || !tf_ctx_require_callable(ctx, 0) ||
        !tf_ctx_require_sequence(ctx, 2)) {
        return TF_ERR;
    }
    tf_obj *pred = tf_stack_peek(ctx, 0);
    tf_obj *l2 = tf_stack_peek(ctx, 1);
    tf_obj *l1 = tf_stack_peek(ctx, 2);
    if (!is_sequence(l2)) {
        tf_ctx_runtime_errorf(ctx,
                              "'%s' expected sequence at stack depth 1, found %s\n",
                              ctx->current_word, tf_obj_type_name(l2));
        return TF_ERR;
    }
    if (l1->type != l2->type) {
        tf_ctx_runtime_errorf(
            ctx, "'%s' expected matching sequence types, found %s and %s\n",
            ctx->current_word, tf_obj_type_name(l1), tf_obj_type_name(l2));
        return TF_ERR;
    }
    pred = tf_stack_pop(ctx);
    l2 = tf_stack_pop(ctx);
    l1 = tf_stack_pop(ctx);

    merge_state *state = control_state_acquire(sizeof(*state));
    state->pred = pred;
    state->l1 = l1;
    state->l2 = l2;
    tf_sequence_iter_init(&state->iter1, l1);
    tf_sequence_iter_init(&state->iter2, l2);
    state->o1 = NULL;
    state->o2 = NULL;
    state->string_result = l1->type == TF_OBJ_TYPE_STR;
    state->list_result = l1->type == TF_OBJ_TYPE_LIST;
    state->vector_result = state->string_result || state->list_result
                               ? NULL
                               : tf_obj_new_vector_with_capacity(
                                     sequence_len(l1) + sequence_len(l2));
    tf_list_builder_init(&state->list_builder);
    state->str_result.ptr = NULL;
    state->str_result.len = 0;
    state->str_result.cap = 0;
    if (state->string_result) {
        bytebuf_init(&state->str_result, l1->str.len + l2->str.len);
    }
    predicate_eval_init_reusable(ctx, &state->pred_eval);

    tf_frame_push_native(ctx, merge_step, merge_cleanup, state);
    return TF_OK;
}

tf_ret tf_filter(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 1) || !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *pred = tf_stack_peek(ctx, 0);
    tf_obj *seq = tf_stack_peek(ctx, 1);

    pred = tf_stack_pop(ctx);
    seq = tf_stack_pop(ctx);

    filter_state *state = control_state_acquire(sizeof(*state));
    state->pred = pred;
    state->seq = seq;
    tf_sequence_iter_init(&state->iter, seq);
    state->current = NULL;
    state->string_result = seq->type == TF_OBJ_TYPE_STR;
    state->list_result = seq->type == TF_OBJ_TYPE_LIST;
    state->vector_result = state->string_result || state->list_result
                               ? NULL
                               : tf_obj_new_vector();
    tf_list_builder_init(&state->list_builder);
    state->str_result.ptr = NULL;
    state->str_result.len = 0;
    state->str_result.cap = 0;
    if (state->string_result) bytebuf_init(&state->str_result, seq->str.len);
    predicate_eval_init_reusable(ctx, &state->pred_eval);

    tf_frame_push_native(ctx, filter_step, filter_cleanup, state);
    return TF_OK;
}

tf_ret tf_some(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 1) || !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *pred = tf_stack_peek(ctx, 0);
    tf_obj *seq = tf_stack_peek(ctx, 1);

    pred = tf_stack_pop(ctx);
    seq = tf_stack_pop(ctx);

    quantifier_state *state = control_state_acquire(sizeof(*state));
    state->pred = pred;
    state->seq = seq;
    tf_sequence_iter_init(&state->iter, seq);
    state->current = NULL;
    state->kind = TF_QUANT_SOME;
    predicate_eval_init_reusable(ctx, &state->pred_eval);

    tf_frame_push_native(ctx, quantifier_step, quantifier_cleanup, state);
    return TF_OK;
}

tf_ret tf_all(tf_ctx *ctx) {
    if (!tf_ctx_require_sequence(ctx, 1) || !tf_ctx_require_callable(ctx, 0)) {
        return TF_ERR;
    }
    tf_obj *pred = tf_stack_peek(ctx, 0);
    tf_obj *seq = tf_stack_peek(ctx, 1);

    pred = tf_stack_pop(ctx);
    seq = tf_stack_pop(ctx);

    quantifier_state *state = control_state_acquire(sizeof(*state));
    state->pred = pred;
    state->seq = seq;
    tf_sequence_iter_init(&state->iter, seq);
    state->current = NULL;
    state->kind = TF_QUANT_ALL;
    predicate_eval_init_reusable(ctx, &state->pred_eval);

    tf_frame_push_native(ctx, quantifier_step, quantifier_cleanup, state);
    return TF_OK;
}

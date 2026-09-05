#ifndef TF_OBJ_H
#define TF_OBJ_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    TF_OBJ_TYPE_BOOL,
    TF_OBJ_TYPE_INT,
    TF_OBJ_TYPE_FLOAT,
    TF_OBJ_TYPE_STR,
    TF_OBJ_TYPE_SYMBOL,
    TF_OBJ_TYPE_CALL,
    TF_OBJ_TYPE_VECTOR,
    TF_OBJ_TYPE_LIST,
    TF_OBJ_TYPE_MAP,
    TF_OBJ_TYPE_SET,
    TF_OBJ_TYPE_DEQUE,
    TF_OBJ_TYPE_PQUEUE,
    TF_OBJ_TYPE_RESOURCE,
    TF_OBJ_TYPE_VARLIST,
    TF_OBJ_TYPE_VARFETCH
} tf_type;

typedef struct tf_source_file tf_source_file;

/* State-owned reusable storage. Fields are opaque outside the object layer;
 * they live here so the interpreter context can embed the pool directly. */
typedef struct {
    void *obj_cache;
    size_t obj_cache_len;
    void *list_node_slabs;
    void *list_node_available_slabs;
    size_t list_node_spare_bytes;
} tf_obj_pool;

typedef struct {
    tf_source_file *source;
    uint32_t offset;
    uint32_t line;
    uint32_t col;
    uint32_t len;
} tf_source_span;

/*
 * Boxed, refcounted runtime value. Small integers may instead use the tagged
 * `tf_obj *` representation described below.
 *
 * Vectors back indexed data and executable quotations. Lists are persistent
 * cons-style sequences. Symbols are inert names; calls use the same string
 * storage but execute their named word when encountered in a program.
 */
typedef struct tf_obj tf_obj;
typedef void (*tf_obj_write_fn)(void *userdata, const char *data,
                                size_t length);
typedef void (*tf_resource_destructor)(void *resource, void *userdata);

typedef struct {
    tf_obj *key;
    tf_obj *value;
    uint64_t hash;
} tf_map_entry;

typedef struct {
    tf_obj *item;
    uint64_t hash;
} tf_set_entry;

typedef struct {
    tf_obj *priority;
    size_t order;
    tf_obj *value;
} tf_pqueue_entry;

typedef struct tf_list_node {
    int refcount;
    tf_obj *value;
    struct tf_list_node *next;
} tf_list_node;

typedef struct {
    tf_list_node *head;
    tf_list_node **next;
    size_t len;
} tf_list_builder;

/* Two inline slots fit inside the union space already required by maps. */
#define TF_VECTOR_INLINE_CAP 2

/* Heap strings reuse these otherwise-idle bytes to store their capacity. */
#define TF_STRING_INLINE_CAP (sizeof(void *) == 8 ? 22 : 10)

struct tf_obj {
    int refcount;
    tf_type type;
    tf_source_span span;
    union {
        int64_t i;
        double f;
        bool b;
        struct {
            char *ptr;
            size_t len;
            char inline_buf[TF_STRING_INLINE_CAP + 1];
        } str;
        struct {
            struct tf_obj **elem;
            size_t len;
            size_t cap;
            struct tf_obj *inline_elem[TF_VECTOR_INLINE_CAP];
        } vector;
        struct {
            tf_list_node *head;
            size_t len;
        } list;
        struct {
            tf_map_entry *entries;
            size_t len;
            size_t cap;
            size_t *buckets;
            size_t bucket_cap;
        } map;
        struct {
            tf_set_entry *entries;
            size_t len;
            size_t cap;
            size_t *buckets;
            size_t bucket_cap;
        } set;
        struct {
            struct tf_obj **elem;
            size_t len;
            size_t cap;
            size_t head;
        } deque;
        struct {
            tf_pqueue_entry *entries;
            size_t len;
            size_t cap;
            size_t next_order;
        } pqueue;
        struct {
            char *type_name;
            size_t type_len;
            void *pointer;
            tf_resource_destructor destructor;
            void *destructor_userdata;
        } resource;
    };
};

_Static_assert(sizeof(((tf_obj *)0)->vector) <= sizeof(((tf_obj *)0)->map),
               "inline vector storage must not increase tf_obj size");
_Static_assert(sizeof(((tf_obj *)0)->str) <= sizeof(((tf_obj *)0)->map),
               "inline string storage must not increase tf_obj size");
_Static_assert(sizeof(((tf_obj *)0)->resource) <= sizeof(((tf_obj *)0)->map),
               "resource storage must not increase tf_obj size");
_Static_assert(_Alignof(tf_obj) >= 2,
               "boxed object pointers must leave the low tag bit clear");

/* On 64-bit targets, values in the signed 63-bit range use the low pointer bit
 * as an immediate-integer tag. Full-width outliers remain boxed. */
static inline bool tf_obj_is_immediate_int(const tf_obj *o) {
#if UINTPTR_MAX == UINT64_MAX
    return o && (((uintptr_t)o & (uintptr_t)1) != 0);
#else
    (void)o;
    return false;
#endif
}

static inline tf_type tf_obj_typeof(const tf_obj *o) {
    return tf_obj_is_immediate_int(o) ? TF_OBJ_TYPE_INT : o->type;
}

static inline int64_t tf_obj_int_value(const tf_obj *o) {
    if (!tf_obj_is_immediate_int(o)) return o->i;
#if UINTPTR_MAX == UINT64_MAX
    uint64_t payload = (uint64_t)(uintptr_t)o >> 1;
    if ((payload & (UINT64_C(1) << 62)) == 0) return (int64_t)payload;
    uint64_t magnitude = (~payload + 1) & ((UINT64_C(1) << 63) - 1);
    if (magnitude == (UINT64_C(1) << 62)) {
        return -(INT64_C(4611686018427387903)) - 1;
    }
    return -(int64_t)magnitude;
#else
    return o->i;
#endif
}

static inline tf_source_span tf_obj_span(const tf_obj *o) {
    return tf_obj_is_immediate_int(o) ? (tf_source_span){0} : o->span;
}

/* Non-owning cursor over a sequence owned by the caller. */
typedef struct {
    tf_obj *sequence;
    size_t index;
    tf_list_node *node;
} tf_sequence_iter;

/* Object constructors. Boxed results start with refcount 1; ownership helpers
 * apply uniformly to immediate integers. */
tf_obj *tf_obj_new(int type);
tf_obj *tf_obj_new_vector(void);
tf_obj *tf_obj_new_vector_with_capacity(size_t capacity);
tf_obj *tf_obj_new_list(void);
tf_obj *tf_obj_new_map(void);
tf_obj *tf_obj_new_set(void);
tf_obj *tf_obj_new_deque(void);
tf_obj *tf_obj_new_pqueue(void);
/* Always boxes; the parser uses this to retain literal source metadata. */
tf_obj *tf_obj_new_int_boxed(int64_t i);

static inline tf_obj *tf_obj_new_int(int64_t i) {
#if UINTPTR_MAX == UINT64_MAX
    const int64_t immediate_min = -(INT64_C(4611686018427387903)) - 1;
    const int64_t immediate_max = INT64_C(4611686018427387903);
    if (i >= immediate_min && i <= immediate_max) {
        uintptr_t encoded = ((uintptr_t)(uint64_t)i << 1) | (uintptr_t)1;
        return (tf_obj *)encoded;
    }
#endif
    return tf_obj_new_int_boxed(i);
}

tf_obj *tf_obj_new_bool(bool b);
tf_obj *tf_obj_new_float(double f);
tf_obj *tf_obj_new_symbol(const char *s, size_t len);
tf_obj *tf_obj_new_call(const char *s, size_t len);
tf_obj *tf_obj_new_string(const char *s, size_t len);
tf_obj *tf_obj_new_string_uninitialized(size_t len);
/* Takes a heap buffer of at least len + 1 bytes and always consumes it. */
tf_obj *tf_obj_new_string_take(char *s, size_t len);
tf_obj *tf_obj_new_resource(const char *type_name, size_t type_len,
                            void *pointer, tf_resource_destructor destructor,
                            void *destructor_userdata);
tf_source_file *tf_source_file_new(const char *filename);
void tf_source_file_retain(tf_source_file *source);
void tf_source_file_release(tf_source_file *source);
const char *tf_source_file_name(tf_source_file *source);
void tf_source_file_set_import_base(tf_source_file *source,
                                    const char *directory);
const char *tf_source_file_import_base(tf_source_file *source);
void tf_source_file_add_doc(tf_source_file *source, uint32_t offset,
                            const char *description, size_t description_len);
const char *tf_source_file_doc_at(tf_source_file *source, uint32_t offset);
void tf_source_file_set_package(tf_source_file *source, size_t package_index);
size_t tf_source_file_package(tf_source_file *source);
void tf_obj_set_span(tf_obj *o, tf_source_span span);

/* String/symbol/call comparison and vector storage helpers. */
int tf_obj_compare_string(tf_obj *a, tf_obj *b);
int tf_obj_compare_number(tf_obj *a, tf_obj *b);
void tf_format_double(char *buf, size_t size, double value);
void tf_string_reserve(tf_obj *s, size_t capacity);
void tf_vector_reserve(tf_obj *v, size_t capacity);

/* Transfer ownership without changing refcounts. Keep the common storage
 * operations visible to callers; only growth needs an out-of-line call. */
static inline void tf_vector_push(tf_obj *v, tf_obj *elem) {
    if (v->vector.len == v->vector.cap) {
        tf_vector_reserve(v, v->vector.len + 1);
    }
    v->vector.elem[v->vector.len++] = elem;
}

static inline tf_obj *tf_vector_pop(tf_obj *v) {
    if (v->vector.len == 0) return NULL;
    /* Stack pops retain capacity so removal never reallocates. */
    return v->vector.elem[--v->vector.len];
}

static inline tf_obj *tf_vector_pop_type(tf_obj *v, tf_type type) {
    if (v->vector.len == 0) return NULL;
    tf_obj *o = v->vector.elem[v->vector.len - 1];
    if (tf_obj_typeof(o) != type) return NULL;
    return tf_vector_pop(v);
}

tf_obj *tf_vector_clone(tf_obj *src);

/* Equality, hashing, and collection storage helpers. */
bool tf_obj_equal(tf_obj *a, tf_obj *b);
bool tf_obj_hashable(tf_obj *o);
uint64_t tf_obj_hash(tf_obj *o);
void tf_map_reserve(tf_obj *map, size_t capacity);
tf_obj *tf_map_clone(tf_obj *map);
bool tf_map_has(tf_obj *map, tf_obj *key);
bool tf_map_get(tf_obj *map, tf_obj *key, tf_obj **out);
bool tf_map_set(tf_obj *map, tf_obj *key, tf_obj *value);
bool tf_map_remove(tf_obj *map, tf_obj *key);
void tf_set_reserve(tf_obj *set, size_t capacity);
tf_obj *tf_set_clone(tf_obj *set);
bool tf_set_has(tf_obj *set, tf_obj *item);
bool tf_set_add(tf_obj *set, tf_obj *item);
bool tf_set_remove(tf_obj *set, tf_obj *item);
tf_obj *tf_list_from_vector(tf_obj *vector);
tf_obj *tf_list_cons_obj(tf_obj *head, tf_obj *tail);
tf_obj *tf_list_rest_obj(tf_obj *list);
tf_obj *tf_list_take_obj(tf_obj *list, size_t count);
tf_obj *tf_list_drop_obj(tf_obj *list, size_t count);
tf_obj *tf_list_concat_obj(tf_obj *left, tf_obj *right);
tf_obj *tf_list_push_back_obj(tf_obj *list, tf_obj *item);
tf_obj *tf_list_reverse_obj(tf_obj *list);
tf_obj *tf_list_get(tf_obj *list, size_t idx);
void tf_list_builder_init(tf_list_builder *builder);
void tf_list_builder_push_owned(tf_list_builder *builder, tf_obj *item);
tf_obj *tf_list_builder_finish(tf_list_builder *builder);
void tf_list_builder_cleanup(tf_list_builder *builder);
void tf_sequence_iter_init(tf_sequence_iter *iter, tf_obj *sequence);
tf_obj *tf_sequence_iter_next_owned(tf_sequence_iter *iter);
void tf_deque_reserve(tf_obj *deque, size_t capacity);
tf_obj *tf_deque_clone(tf_obj *src);
tf_obj *tf_deque_get(tf_obj *deque, size_t idx);
void tf_deque_push_front(tf_obj *deque, tf_obj *value);
void tf_deque_push_back(tf_obj *deque, tf_obj *value);
tf_obj *tf_deque_pop_front(tf_obj *deque);
tf_obj *tf_deque_pop_back(tf_obj *deque);
void tf_pqueue_reserve(tf_obj *pqueue, size_t capacity);
tf_obj *tf_pqueue_clone(tf_obj *src);
void tf_pqueue_append(tf_obj *pqueue, tf_obj *priority, tf_obj *value);
void tf_pqueue_heapify(tf_obj *pqueue);
void tf_pqueue_push(tf_obj *pqueue, tf_obj *priority, tf_obj *value);
bool tf_pqueue_peek(tf_obj *pqueue, tf_obj **priority, tf_obj **value);
bool tf_pqueue_pop(tf_obj *pqueue, tf_obj **priority, tf_obj **value);

/* Reference-count ownership. Immediate integers make both operations no-ops;
 * keep that path inline because stack and frame movement call it frequently. */
void tf_obj_free(tf_obj *o);

static inline void tf_obj_retain(tf_obj *o) {
    if (tf_obj_is_immediate_int(o)) return;
    o->refcount++;
}

static inline void tf_obj_release(tf_obj *o) {
    if (tf_obj_is_immediate_int(o)) return;
    assert(o->refcount > 0);
    o->refcount--;
    if (o->refcount == 0) tf_obj_free(o);
}

/* Constructors use the current thread's scoped pool. Allocation ownership is
 * stored outside tf_obj, so release returns storage to the originating state
 * even when another pool is current. A null current pool uses malloc/free. */
void tf_obj_pool_init(tf_obj_pool *pool);
void tf_obj_pool_clear(tf_obj_pool *pool);
tf_obj_pool *tf_obj_pool_enter(tf_obj_pool *pool);
void tf_obj_pool_leave(tf_obj_pool *previous);

/* Debug, runtime-value, and source-form printers. */
void tf_obj_print(tf_obj *o, size_t *count);
void tf_obj_print_value(tf_obj *o);
void tf_obj_print_display(tf_obj *o);
void tf_obj_print_source(tf_obj *o);
void tf_obj_fprint_display(FILE *output, tf_obj *o);
void tf_obj_print_value_colored(tf_obj *o);
void tf_obj_print_display_colored(tf_obj *o);
void tf_obj_print_source_colored(tf_obj *o);
void tf_obj_write_value(tf_obj *o, tf_obj_write_fn write, void *userdata,
                        bool color);
void tf_obj_write_display(tf_obj *o, tf_obj_write_fn write, void *userdata,
                          bool color);
void tf_obj_write_source(tf_obj *o, tf_obj_write_fn write, void *userdata,
                         bool color);

#endif  // TF_OBJ_H

#ifndef TF_EXEC_H
#define TF_EXEC_H

#include <signal.h>
#include "toy.h"
#include "tf_docs.h"
#include "tf_obj.h"
#include "tf_random.h"

typedef toy_status tf_ret;
typedef toy_state tf_ctx;
typedef toy_native_fn tf_native_fn;

#define TF_OK TOY_OK
#define TF_ERR TOY_ERROR
#define TF_INTERRUPTED TOY_INTERRUPTED
#define TF_EXIT_REQUESTED TOY_EXIT_REQUESTED
typedef tf_ret (*tf_frame_step_fn)(tf_ctx *ctx, void *state, bool *done);
typedef void (*tf_frame_cleanup_fn)(tf_ctx *ctx, void *state, tf_ret status);
typedef tf_ret (*tf_frame_error_fn)(tf_ctx *ctx, void *state, tf_ret status,
                                    bool *handled);

typedef enum { TF_DEBUG_STEP, TF_DEBUG_CONTINUE, TF_DEBUG_ABORT } tf_debug_action;

typedef struct {
    tf_obj *instruction;
    tf_source_span span;
    size_t pc;
    size_t frame_depth;
} tf_debug_event;

typedef tf_debug_action (*tf_debug_hook_fn)(tf_ctx *ctx,
                                             const tf_debug_event *event,
                                             void *userdata);

/* Native registration metadata shared with interactive tooling. */
typedef struct {
    const char *name;
    tf_native_fn cb;
} tf_builtin_word;

typedef struct {
    const char *title;
    const tf_builtin_word *words;
} tf_builtin_group;

typedef enum { TF_WORD_NATIVE, TF_WORD_USER } tf_word_kind;
#define TF_WORD_LOOKUP_CACHE_CAP 64
#define TF_QUICK_PROGRAM_CACHE_CAP 256
#define TF_QUICK_PROGRAM_CACHE_WAYS 4
#define TF_QUICK_PROGRAM_CACHE_SETS \
    (TF_QUICK_PROGRAM_CACHE_CAP / TF_QUICK_PROGRAM_CACHE_WAYS)
#define TF_ROOT_PACKAGE 0

typedef enum {
    TF_PACKAGE_LOADING,
    TF_PACKAGE_LOADED,
    TF_PACKAGE_FAILED
} tf_package_state;

typedef struct {
    char *name;
    size_t name_len;
    char *path;
    tf_package_state state;
} tf_package;

typedef struct {
    tf_package *entries;
    size_t len;
    size_t cap;
} tf_package_table;

typedef struct {
    void **handles;
    size_t len;
    size_t cap;
} tf_native_library_table;

typedef struct {
    char *name;
    size_t name_len;
    size_t owner_package_index;
    size_t target_package_index;
} tf_package_import;

typedef struct {
    tf_package_import *entries;
    size_t len;
    size_t cap;
} tf_package_import_table;

/*
 * Global dictionary entry.
 *
 * Native words point at C functions. User words point at Toy quotations.
 * Builtin names may be borrowed static strings; dynamically registered and
 * user-defined names are copied when `owns_name` is true.
 */
typedef struct {
    const char *name;
    size_t name_len;
    bool owns_name;
    size_t package_index;
    bool is_public;
    char *doc_stack_effect;
    char *doc_description;
    tf_word_kind type;
    union {
        tf_native_fn native_impl;
        tf_obj *user_impl;
    };
} tf_word;

typedef void (*tf_visible_word_fn)(const char *display_name,
                                   size_t display_name_len, tf_word *word,
                                   void *userdata);

typedef struct {
    tf_obj *key;
    size_t package_index;
    size_t generation;
    size_t entry_index;
} tf_word_lookup_cache_entry;

/*
 * Open-addressed global word dictionary.
 *
 * Definitions live in a dense array; buckets store one-based entry indexes.
 * Lookup-cache keys retain call/symbol objects, so object-cache address reuse
 * cannot turn an entry into a false hit. Cached dense indexes survive entry
 * array reallocations and are guarded by lexical package and resolution
 * generation.
 * A tf_word* returned by lookup is transient and must not be retained across
 * dictionary mutation.
 */
typedef struct {
    tf_word *entries;
    size_t entry_capacity;
    size_t *buckets;
    size_t capacity;
    size_t count;
    size_t resolution_generation;
    tf_word_lookup_cache_entry lookup_cache[TF_WORD_LOOKUP_CACHE_CAP];
} tf_word_table;

typedef struct {
    tf_obj *name;
    tf_obj *val;
} tf_var;

typedef enum {
    TF_QUICK_CALL_WORD,
    TF_QUICK_CALL_NATIVE,
    TF_QUICK_CALL_DUP,
    TF_QUICK_CALL_PRED,
    TF_QUICK_CALL_ADD,
    TF_QUICK_CALL_MUL,
    TF_QUICK_CALL_LT
} tf_quick_call_kind;

typedef struct {
    size_t generation;
    /* Native code stays loaded for the context's lifetime. User definitions
     * use indexes into relocatable dictionary storage. Both are valid only
     * while the resolution generation matches. */
    union {
        size_t entry_index;
        tf_native_fn native_impl;
    };
    tf_quick_call_kind kind;
} tf_quick_call;

typedef struct {
    size_t refcount;
    tf_obj *program;
    size_t package_index;
    size_t len;
    tf_quick_call calls[];
} tf_quick_program;

#ifdef TF_OBSERVE
typedef struct {
    uint64_t instructions;
    uint64_t call_instructions;
    uint64_t native_continuation_steps;
    uint64_t native_word_calls;
    uint64_t user_word_calls;
    uint64_t program_frames;
    uint64_t native_frames;
    uint64_t max_frame_depth;
    uint64_t max_data_stack_depth;
    uint64_t dictionary_lookups;
    uint64_t dictionary_cache_hits;
    uint64_t dictionary_cache_misses;
    uint64_t quick_program_cache_hits;
    uint64_t quick_program_cache_misses;
    uint64_t quick_program_cache_evictions;
    uint64_t quickened_lookup_hits;
    uint64_t quickened_lookup_misses;
    uint64_t specialized_dispatches;
    uint64_t general_dispatches;
} tf_runtime_metrics;

#define TF_METRIC_INC(ctx, field) ((ctx)->metrics.field++)
#define TF_METRIC_MAX(ctx, field, value)                                  \
    do {                                                                  \
        uint64_t tf_metric_value = (uint64_t)(value);                      \
        if ((ctx)->metrics.field < tf_metric_value) {                      \
            (ctx)->metrics.field = tf_metric_value;                        \
        }                                                                 \
    } while (0)
#else
#define TF_METRIC_INC(ctx, field) ((void)0)
#define TF_METRIC_MAX(ctx, field, value) ((void)0)
#endif

typedef struct tf_scratch_block tf_scratch_block;

typedef struct {
    tf_scratch_block *current;
    tf_scratch_block *spare;
    size_t spare_bytes;
    size_t depth;
} tf_scratch_arena;

typedef struct {
    /* This frame owns a segment of ctx->captures, addressed by index so both
     * frame and binding storage can relocate independently. */
    size_t base;
    size_t len;
} tf_var_table;

typedef enum {
    TF_FRAME_PROGRAM,
    TF_FRAME_PROGRAM_ROOT,
    TF_FRAME_PROGRAM_USER,
    TF_FRAME_NATIVE,
    TF_FRAME_PROGRAM_REPEAT
} tf_frame_kind;

typedef struct {
    tf_obj *program;
    size_t pc;
    size_t package_index;
    tf_quick_program *quick;
    tf_var_table vars;
    size_t repeats;
} tf_program_frame;

typedef struct {
    tf_frame_step_fn step;
    tf_frame_cleanup_fn cleanup;
    tf_frame_error_fn on_error;
    void *state;
} tf_native_frame;

/*
 * Execution frame.
 *
 * Program frames evaluate Toy vectors with a program counter and dynamic capture
 * bindings. Native frames are continuations used by C words that need to resume
 * after scheduled Toy code finishes.
 */
typedef struct {
    tf_frame_kind kind;
    union {
        tf_program_frame program;
        tf_native_frame native;
    } as;
    tf_source_span call_site;
} tf_frame;

typedef struct {
    tf_frame_kind kind;
    const char *word_name;
    tf_source_span call_site;
    tf_source_span location;
    size_t pc;
    size_t program_len;
} tf_debug_frame_info;

typedef struct {
    const char *name;
    tf_obj *value;
} tf_debug_capture_info;

typedef struct {
    const char *name;
    bool user_defined;
    tf_obj *body;
} tf_debug_word_info;

typedef struct tf_deferred_call tf_deferred_call;

/*
 * Interpreter state shared by the VM and native words.
 */
struct tf_ctx {
    /* Values and dictionary. */
    tf_obj_pool objects;
    tf_obj *data_stack;
    tf_word_table words;

    /* Iterative execution and its context-local caches. */
    tf_quick_program *quick_programs[TF_QUICK_PROGRAM_CACHE_CAP];
    tf_frame *call_stack;  // explicit execution stack
    size_t call_stack_len;
    size_t call_stack_cap;
    tf_var *captures;  // active dynamic bindings, in frame/binding order
    size_t captures_len;
    size_t captures_cap;
    tf_deferred_call *deferred_head;
    tf_deferred_call *deferred_tail;
    size_t deferred_count;
    bool deferred_active;
    tf_scratch_arena scratch;
    void *control_state_cache;
    size_t control_state_cache_len;
#ifdef TF_OBSERVE
    tf_runtime_metrics metrics;
#endif

    /* State services, packages, and loaded native libraries. */
    tf_random random;
    tf_package_table packages;
    tf_package_import_table package_imports;
    tf_native_library_table native_libraries;
    char *core_package_path;

    /* Host inputs and execution status. */
    int argc;
    char **argv;
    size_t error_suppression_depth;
    bool error_reported;
    bool suppress_repl_status;
    volatile sig_atomic_t interrupted;
    char *last_error;
    tf_source_span current_span;
    const char *current_word;

    /* Debugger integration. */
    tf_debug_hook_fn debug_hook;
    void *debug_userdata;

    /* Output and diagnostic destinations. */
    toy_write_fn output;
    void *output_userdata;
    toy_write_fn diagnostic;
    void *diagnostic_userdata;
    bool output_is_console;
    bool diagnostic_is_console;
};

/* Data stack API used by native word implementations. Push/pop transfer an
 * owned reference; peek borrows. Pop/peek return NULL on underflow. Operations
 * are O(1), with amortized O(1) pushes through geometric vector growth. */
static inline size_t tf_stack_len(tf_ctx *ctx) {
    return ctx->data_stack->vector.len;
}

static inline void tf_stack_push(tf_ctx *ctx, tf_obj *o) {
    tf_vector_push(ctx->data_stack, o);
    TF_METRIC_MAX(ctx, max_data_stack_depth, ctx->data_stack->vector.len);
}

static inline tf_obj *tf_stack_pop(tf_ctx *ctx) {
    return tf_vector_pop(ctx->data_stack);
}

static inline tf_obj *tf_stack_pop_type(tf_ctx *ctx, tf_type type) {
    return tf_vector_pop_type(ctx->data_stack, type);
}

static inline tf_obj *tf_stack_peek(tf_ctx *ctx, size_t depth) {
    size_t len = tf_stack_len(ctx);
    if (depth >= len) return NULL;
    return ctx->data_stack->vector.elem[len - 1 - depth];
}

tf_obj *tf_stack_pop_callable(tf_ctx *ctx);

/*
 * Native-word validation helpers.
 *
 * Depth is counted from the top of the stack: depth 0 is the next value that
 * would be popped. Helpers report a ctx-aware diagnostic and return false on
 * failure; callers should then return TF_ERR without modifying the stack.
 */
const char *tf_type_name(tf_type type);
const char *tf_obj_type_name(tf_obj *o);

/* Success stays with the caller so validation and the following stack access
 * can share loads. Diagnostic formatting lives in the cold error helpers. */
bool tf_ctx_stack_error(tf_ctx *ctx, size_t needed);
bool tf_ctx_type_error(tf_ctx *ctx, size_t depth, const char *expected);

static inline bool tf_obj_is_callable(tf_obj *o) {
    if (!o) return false;
    tf_type type = tf_obj_typeof(o);
    return type == TF_OBJ_TYPE_VECTOR || type == TF_OBJ_TYPE_SYMBOL ||
           type == TF_OBJ_TYPE_CALL;
}

static inline bool tf_ctx_require_stack(tf_ctx *ctx, size_t needed) {
    if (tf_stack_len(ctx) >= needed) return true;
    return tf_ctx_stack_error(ctx, needed);
}

static inline bool tf_ctx_require_type(tf_ctx *ctx, size_t depth, tf_type type) {
    tf_obj *o = tf_stack_peek(ctx, depth);
    if (o && tf_obj_typeof(o) == type) return true;
    return tf_ctx_type_error(ctx, depth, tf_type_name(type));
}

static inline bool tf_ctx_require_number(tf_ctx *ctx, size_t depth) {
    tf_obj *o = tf_stack_peek(ctx, depth);
    if (o) {
        tf_type type = tf_obj_typeof(o);
        if (type == TF_OBJ_TYPE_INT || type == TF_OBJ_TYPE_FLOAT) return true;
    }
    return tf_ctx_type_error(ctx, depth, "number");
}

static inline bool tf_ctx_require_sequence(tf_ctx *ctx, size_t depth) {
    tf_obj *o = tf_stack_peek(ctx, depth);
    if (o) {
        tf_type type = tf_obj_typeof(o);
        if (type == TF_OBJ_TYPE_VECTOR || type == TF_OBJ_TYPE_LIST ||
            type == TF_OBJ_TYPE_STR) return true;
    }
    return tf_ctx_type_error(ctx, depth, "sequence");
}

static inline bool tf_ctx_require_callable(tf_ctx *ctx, size_t depth) {
    if (tf_obj_is_callable(tf_stack_peek(ctx, depth))) return true;
    return tf_ctx_type_error(ctx, depth, "callable");
}

/* Execution frame scheduling. Native words schedule work here, then return. */
void tf_frame_push_program(tf_ctx *ctx, tf_obj *program);
void tf_frame_push_program_package(tf_ctx *ctx, tf_obj *program,
                                   size_t package_index);
bool tf_frame_push_repeat(tf_ctx *ctx, tf_obj *program, int64_t count);
void tf_frame_push_native(tf_ctx *ctx, tf_frame_step_fn step,
                          tf_frame_cleanup_fn cleanup, void *state);
void tf_frame_push_native_handler(tf_ctx *ctx, tf_frame_step_fn step,
                                  tf_frame_cleanup_fn cleanup,
                                  tf_frame_error_fn on_error, void *state);
void tf_frame_pop(tf_ctx *ctx, tf_ret status);
void tf_quick_program_cache_clear(tf_ctx *ctx);

/* Same-thread calls owned by the VM and started between instructions. */
void tf_deferred_call_enqueue(tf_ctx *ctx, tf_obj *callable,
                              size_t argument_count);
size_t tf_deferred_call_count(tf_ctx *ctx);
void tf_deferred_calls_clear(tf_ctx *ctx);

/* Strict-LIFO scratch storage owned by native continuation frames. */
void *tf_scratch_alloc(tf_ctx *ctx, size_t size);
void tf_scratch_release(tf_ctx *ctx, void *ptr);
void tf_scratch_clear(tf_ctx *ctx);

/* Context lifecycle. */
tf_ctx *tf_ctx_new(int argc, char **argv);
void tf_ctx_free(tf_ctx *ctx);
void tf_ctx_interrupt(tf_ctx *ctx);
void tf_ctx_clear_error(tf_ctx *ctx);
void tf_ctx_set_error(tf_ctx *ctx, const char *message);
const char *tf_ctx_last_error(tf_ctx *ctx);
void tf_ctx_set_output(tf_ctx *ctx, toy_write_fn output, void *userdata);
void tf_ctx_set_diagnostic(tf_ctx *ctx, toy_write_fn diagnostic,
                           void *userdata);
void tf_ctx_write_output(tf_ctx *ctx, const char *data, size_t length);
void tf_ctx_outputf(tf_ctx *ctx, const char *fmt, ...);
void tf_ctx_write_diagnostic(tf_ctx *ctx, const char *data, size_t length);
void tf_ctx_diagnosticf(tf_ctx *ctx, const char *fmt, ...);
bool tf_ctx_output_is_console(tf_ctx *ctx);

/* Read-only native catalog, in presentation order. */
const tf_builtin_group *tf_builtin_groups(size_t *count);

/* Global word dictionary. */
void tf_dict_init(tf_ctx *ctx, size_t initial_word_count);
void tf_dict_free(tf_ctx *ctx);
void tf_dict_set_native(tf_ctx *ctx, const char *name, tf_native_fn cb);
void tf_dict_set_native_copy(tf_ctx *ctx, const char *name, tf_native_fn cb);
void tf_dict_add_native_scoped(tf_ctx *ctx, const char *name, size_t name_len,
                               size_t package_index, tf_native_fn cb,
                               const char *stack_effect,
                               const char *description);
bool tf_dict_set_doc_scoped(tf_ctx *ctx, size_t package_index,
                            const char *name, size_t name_len,
                            const char *stack_effect,
                            const char *description);
bool tf_dict_set_user(tf_ctx *ctx, tf_obj *name, tf_obj *uf);
bool tf_dict_set_user_in_package(tf_ctx *ctx, size_t package_index,
                                 tf_obj *name, tf_obj *uf);
bool tf_dict_make_private(tf_ctx *ctx, tf_obj *name);
bool tf_dict_make_private_in_package(tf_ctx *ctx, size_t package_index,
                                     tf_obj *name);
tf_word *tf_dict_lookup(tf_ctx *ctx, tf_obj *name);
tf_word *tf_dict_lookup_from(tf_ctx *ctx, size_t package_index,
                             tf_obj *name);
tf_word *tf_dict_lookup_scoped(tf_ctx *ctx, size_t package_index,
                               const char *name, size_t len);
void tf_dict_resolution_changed(tf_ctx *ctx);
void tf_dict_lookup_cache_clear(tf_ctx *ctx);
void tf_dict_each_visible(tf_ctx *ctx, tf_visible_word_fn visit,
                          void *userdata);
const tf_doc_entry *tf_word_doc(tf_word *word, tf_doc_entry *scratch);

/* Package registry, imports, and active lexical package. */
void tf_packages_init(tf_ctx *ctx);
void tf_packages_free(tf_ctx *ctx);
size_t tf_current_package_index(tf_ctx *ctx);
bool tf_package_name_valid(const char *name, size_t name_len);
bool tf_package_word_name_valid(const char *name, size_t name_len);
size_t tf_package_find_path(tf_ctx *ctx, const char *path);
size_t tf_package_begin(tf_ctx *ctx, const char *name, size_t name_len,
                        const char *path);
size_t tf_package_add_registered(tf_ctx *ctx, const char *name,
                                 size_t name_len, const char *identity);
void tf_package_finish(tf_ctx *ctx, size_t package_index, tf_ret status);
const tf_package *tf_package_get(tf_ctx *ctx, size_t package_index);
size_t tf_package_import_find(tf_ctx *ctx, size_t owner_package_index,
                              const char *name, size_t name_len);
bool tf_package_import_add(tf_ctx *ctx, size_t owner_package_index,
                           const char *name, size_t name_len,
                           size_t target_package_index);
void tf_package_import_remove(tf_ctx *ctx, size_t owner_package_index,
                              const char *name, size_t name_len,
                              size_t target_package_index);
void tf_ctx_set_core_package_path(tf_ctx *ctx, const char *path);
tf_ret tf_package_load(tf_ctx *ctx, const char *request,
                       size_t owner_package_index, const char *alias,
                       size_t alias_len, size_t *package_index);
tf_ret tf_package_run_main(tf_ctx *ctx, const char *path);

/* Validate and install a C-defined package without requiring an idle VM. */
toy_status tf_install_native_package(tf_ctx *ctx, size_t package_index,
                                     const toy_native_package *package);

/* Borrow the innermost dynamic binding, or NULL. Linear in active bindings,
 * independent of intervening capture-free frames. */
tf_obj *tf_scope_lookup_var(tf_ctx *ctx, tf_obj *name);

/* VM entry points. */
tf_ret tf_vm_exec(tf_ctx *ctx, tf_obj *program);
tf_ret tf_vm_exec_package(tf_ctx *ctx, tf_obj *program,
                          size_t package_index);
tf_ret tf_vm_call_callable(tf_ctx *ctx, tf_obj *callable);

#ifdef TF_OBSERVE
bool tf_metrics_write_json(tf_ctx *ctx, FILE *output);
#endif

/*
 * Frontend-neutral debugger hook and read-only state inspection. Returned
 * names and objects are borrowed from ctx and must not be released or retained
 * across execution or dictionary mutation.
 */
void tf_debug_set_hook(tf_ctx *ctx, tf_debug_hook_fn hook, void *userdata);
size_t tf_debug_frame_count(tf_ctx *ctx);
bool tf_debug_get_frame(tf_ctx *ctx, size_t depth,
                        tf_debug_frame_info *info);
size_t tf_debug_capture_count(tf_ctx *ctx, size_t frame_depth);
bool tf_debug_get_capture(tf_ctx *ctx, size_t frame_depth, size_t index,
                          tf_debug_capture_info *info);
bool tf_debug_lookup_capture(tf_ctx *ctx, const char *name, size_t name_len,
                             tf_debug_capture_info *info);
size_t tf_debug_word_count(tf_ctx *ctx);
bool tf_debug_get_word(tf_ctx *ctx, size_t index, tf_debug_word_info *info);
bool tf_debug_find_word(tf_ctx *ctx, const char *name, size_t name_len,
                        tf_debug_word_info *info);

/* Context-aware diagnostics used by the VM and native words. */
void tf_ctx_runtime_errorf(tf_ctx *ctx, const char *fmt, ...);
void tf_ctx_program_errorf(tf_ctx *ctx, const char *fmt, ...);
void tf_ctx_parse_error(tf_ctx *ctx, const char *source_name, size_t line,
                        size_t col, const char *message);

#endif  // TF_EXEC_H

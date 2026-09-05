#include "tf_exec.h"
#include <assert.h>
#ifdef TF_OBSERVE
#include <inttypes.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "tf_alloc.h"
#include "tf_builtins.h"
#include "tf_terminal.h"
#include <signal.h>  // IWYU pragma: keep

#define TF_CALL_STACK_INITIAL_CAP 8
#define TF_CAPTURE_INITIAL_CAP (TF_CAPTURE_INLINE_CAP * 2)
#define TF_ERROR_STACK_LIMIT 8
#define TF_ERROR_FRAME_LIMIT 8
#define TF_SCRATCH_BLOCK_CAPACITY 4096
#define TF_SCRATCH_SPARE_BYTE_LIMIT (64 * 1024)
/* Keep the rare repeat check out of the dispatch loop; inlining it regresses
 * unrelated GCC workloads by disturbing the hot VM layout. */
#if defined(_MSC_VER)
#define TF_NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define TF_NOINLINE __attribute__((noinline))
#else
#define TF_NOINLINE
#endif
static TF_NOINLINE void frame_complete(tf_ctx *ctx);
_Static_assert((TF_QUICK_PROGRAM_CACHE_CAP &
                (TF_QUICK_PROGRAM_CACHE_CAP - 1)) == 0,
               "quick-program cache capacity must be a power of two");
_Static_assert((TF_QUICK_PROGRAM_CACHE_WAYS &
                (TF_QUICK_PROGRAM_CACHE_WAYS - 1)) == 0,
               "quick-program cache ways must be a power of two");
_Static_assert(TF_QUICK_PROGRAM_CACHE_WAYS <=
                   TF_QUICK_PROGRAM_CACHE_CAP,
               "quick-program cache ways exceed its capacity");

struct tf_scratch_block {
    tf_scratch_block *prev;
    tf_scratch_block *spare_next;
    size_t capacity;
    size_t used;
    max_align_t alignment;
};

struct tf_deferred_call {
    tf_obj *callable;
    tf_deferred_call *next;
    size_t argument_count;
    tf_obj *arguments[];
};

typedef union {
    struct {
        tf_scratch_block *block;
        size_t used;
        size_t depth;
    } mark;
    max_align_t alignment;
} tf_scratch_allocation;

static unsigned char *scratch_block_data(tf_scratch_block *block) {
    return (unsigned char *)(block + 1);
}

static tf_scratch_block *scratch_block_acquire(tf_scratch_arena *arena,
                                                size_t needed) {
    tf_scratch_block **slot = &arena->spare;
    while (*slot && (*slot)->capacity < needed) {
        slot = &(*slot)->spare_next;
    }
    if (*slot) {
        tf_scratch_block *block = *slot;
        *slot = block->spare_next;
        arena->spare_bytes -= block->capacity;
        block->prev = NULL;
        block->spare_next = NULL;
        block->used = 0;
        return block;
    }

    size_t capacity = needed > TF_SCRATCH_BLOCK_CAPACITY
                          ? needed
                          : TF_SCRATCH_BLOCK_CAPACITY;
    tf_scratch_block *block = tf_xmalloc(sizeof(*block) + capacity);
    block->prev = NULL;
    block->spare_next = NULL;
    block->capacity = capacity;
    block->used = 0;
    return block;
}

static void scratch_block_release(tf_scratch_arena *arena,
                                  tf_scratch_block *block) {
    block->prev = NULL;
    if (block->capacity > TF_SCRATCH_SPARE_BYTE_LIMIT ||
        arena->spare_bytes >
            TF_SCRATCH_SPARE_BYTE_LIMIT - block->capacity) {
        free(block);
        return;
    }
    block->spare_next = arena->spare;
    arena->spare = block;
    arena->spare_bytes += block->capacity;
}

void *tf_scratch_alloc(tf_ctx *ctx, size_t size) {
    tf_scratch_arena *arena = &ctx->scratch;
    size_t alignment = _Alignof(max_align_t);
    size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);
    size_t needed = sizeof(tf_scratch_allocation) + aligned_size;
    if (!arena->current) {
        arena->current = scratch_block_acquire(arena, 0);
    }

    tf_scratch_block *mark_block = arena->current;
    size_t mark_used = mark_block->used;

    if (arena->current->capacity - arena->current->used < needed) {
        tf_scratch_block *block = scratch_block_acquire(arena, needed);
        block->prev = arena->current;
        arena->current = block;
    }

    unsigned char *storage =
        scratch_block_data(arena->current) + arena->current->used;
    tf_scratch_allocation *allocation = (tf_scratch_allocation *)storage;
    allocation->mark.block = mark_block;
    allocation->mark.used = mark_used;
    allocation->mark.depth = arena->depth;
    arena->current->used += needed;
    arena->depth++;
    return allocation + 1;
}

void tf_scratch_release(tf_ctx *ctx, void *ptr) {
    if (!ptr) return;
    tf_scratch_arena *arena = &ctx->scratch;
    tf_scratch_allocation *allocation =
        (tf_scratch_allocation *)ptr - 1;
    tf_scratch_block *mark_block = allocation->mark.block;
    size_t mark_used = allocation->mark.used;
    size_t mark_depth = allocation->mark.depth;

    assert(arena->depth == mark_depth + 1);
    while (arena->current != mark_block) {
        assert(arena->current);
        tf_scratch_block *block = arena->current;
        arena->current = block->prev;
        scratch_block_release(arena, block);
    }
    assert(arena->current && mark_used <= arena->current->used);
    arena->current->used = mark_used;
    arena->depth = mark_depth;
}

void tf_scratch_clear(tf_ctx *ctx) {
    tf_scratch_arena *arena = &ctx->scratch;
    assert(arena->depth == 0);
    while (arena->current) {
        tf_scratch_block *prev = arena->current->prev;
        free(arena->current);
        arena->current = prev;
    }
    while (arena->spare) {
        tf_scratch_block *next = arena->spare->spare_next;
        free(arena->spare);
        arena->spare = next;
    }
    arena->spare_bytes = 0;
    arena->depth = 0;
}

/* === Data Stack === */

tf_obj *tf_stack_pop_callable(tf_ctx *ctx) {
    if (tf_stack_len(ctx) == 0) return NULL;
    tf_obj *o = tf_stack_peek(ctx, 0);
    if (!tf_obj_is_callable(o)) return NULL;
    return tf_stack_pop(ctx);
}

/* === Deferred Calls === */

void tf_deferred_call_enqueue(tf_ctx *ctx, tf_obj *callable,
                              size_t argument_count) {
    assert(ctx);
    assert(callable);
    assert(tf_obj_is_callable(callable));
    assert(tf_stack_len(ctx) >= argument_count);

    tf_deferred_call *call =
        tf_xmalloc(sizeof(*call) + argument_count * sizeof(*call->arguments));
    call->callable = callable;
    tf_obj_retain(callable);
    call->next = NULL;
    call->argument_count = argument_count;

    size_t first_argument = tf_stack_len(ctx) - argument_count;
    for (size_t i = 0; i < argument_count; i++) {
        tf_obj *argument = ctx->data_stack->vector.elem[first_argument + i];
        tf_obj_retain(argument);
        call->arguments[i] = argument;
    }
    for (size_t i = 0; i < argument_count; i++) {
        tf_obj_release(tf_stack_pop(ctx));
    }

    if (ctx->deferred_tail) {
        ctx->deferred_tail->next = call;
    } else {
        ctx->deferred_head = call;
    }
    ctx->deferred_tail = call;
    ctx->deferred_count++;
}

size_t tf_deferred_call_count(tf_ctx *ctx) {
    return ctx ? ctx->deferred_count : 0;
}

void tf_deferred_calls_clear(tf_ctx *ctx) {
    if (!ctx) return;
    while (ctx->deferred_head) {
        tf_deferred_call *call = ctx->deferred_head;
        ctx->deferred_head = call->next;
        tf_obj_release(call->callable);
        for (size_t i = 0; i < call->argument_count; i++) {
            tf_obj_release(call->arguments[i]);
        }
        free(call);
    }
    ctx->deferred_tail = NULL;
    ctx->deferred_count = 0;
}

/* === Execution Frames === */

static void ensure_call_stack_slot(tf_ctx *ctx) {
    if (ctx->call_stack_len < ctx->call_stack_cap) return;
    ctx->call_stack_cap = ctx->call_stack_cap == 0
                              ? TF_CALL_STACK_INITIAL_CAP
                              : ctx->call_stack_cap * 2;
    ctx->call_stack =
        tf_xrealloc(ctx->call_stack, sizeof(tf_frame) * ctx->call_stack_cap);
}

static bool frame_is_program(tf_frame_kind kind) {
    return kind != TF_FRAME_NATIVE;
}

static void quick_program_release(tf_quick_program *quick) {
    if (!quick) return;
    assert(quick->refcount > 0);
    quick->refcount--;
    if (quick->refcount != 0) return;
    tf_obj_release(quick->program);
    free(quick);
}

void tf_quick_program_cache_clear(tf_ctx *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < ctx->call_stack_len; i++) {
        tf_frame *frame = &ctx->call_stack[i];
        if (!frame_is_program(frame->kind) || !frame->as.program.quick) {
            continue;
        }
        tf_quick_program *quick = frame->as.program.quick;
        memset(quick->calls, 0, quick->len * sizeof(tf_quick_call));
    }
    for (size_t i = 0; i < TF_QUICK_PROGRAM_CACHE_CAP; i++) {
        tf_quick_program *quick = ctx->quick_programs[i];
        if (quick) {
            memset(quick->calls, 0, quick->len * sizeof(tf_quick_call));
        }
        quick_program_release(quick);
        ctx->quick_programs[i] = NULL;
    }
}

static size_t quick_program_set(tf_obj *program, size_t package_index) {
    uintptr_t mixed = (uintptr_t)program >> 4;
    mixed ^= (uintptr_t)package_index;
    mixed ^= mixed >> 7;
    mixed ^= mixed >> 13;
    return (size_t)mixed & (TF_QUICK_PROGRAM_CACHE_SETS - 1);
}

static size_t quick_program_slot(size_t set, size_t way) {
    if (way == 0) return set;
    return TF_QUICK_PROGRAM_CACHE_SETS +
           set * (TF_QUICK_PROGRAM_CACHE_WAYS - 1) + way - 1;
}

static bool program_contains_call(tf_obj *program) {
    for (size_t i = 0; i < program->vector.len; i++) {
        if (tf_obj_typeof(program->vector.elem[i]) == TF_OBJ_TYPE_CALL) {
            return true;
        }
    }
    return false;
}

static tf_quick_program *quick_program_acquire(tf_ctx *ctx, tf_obj *program,
                                               size_t package_index) {
    if (program->vector.len == 0) return NULL;

    size_t set = quick_program_set(program, package_index);
    size_t first_slot = quick_program_slot(set, 0);
    /* Primary ways stay compact; overflow ways preserve MRU order. */
    tf_quick_program *quick = ctx->quick_programs[first_slot];
    if (quick && quick->program == program &&
        quick->package_index == package_index) {
        TF_METRIC_INC(ctx, quick_program_cache_hits);
        quick->refcount++;
        return quick;
    }
    for (size_t way = 1; way < TF_QUICK_PROGRAM_CACHE_WAYS; way++) {
        size_t slot = quick_program_slot(set, way);
        quick = ctx->quick_programs[slot];
        if (!quick || quick->program != program ||
            quick->package_index != package_index) {
            continue;
        }
        TF_METRIC_INC(ctx, quick_program_cache_hits);
        for (size_t i = way; i > 0; i--) {
            ctx->quick_programs[quick_program_slot(set, i)] =
                ctx->quick_programs[quick_program_slot(set, i - 1)];
        }
        ctx->quick_programs[first_slot] = quick;
        quick->refcount++;
        return quick;
    }
    if (!program_contains_call(program)) return NULL;
    TF_METRIC_INC(ctx, quick_program_cache_misses);
    if (program->vector.len >
        (SIZE_MAX - sizeof(*quick)) / sizeof(tf_quick_call)) {
        return NULL;
    }

    quick = tf_xmalloc(sizeof(*quick) +
                       program->vector.len * sizeof(tf_quick_call));
    quick->refcount = 1;
    quick->program = program;
    tf_obj_retain(program);
    quick->package_index = package_index;
    quick->len = program->vector.len;
    memset(quick->calls, 0, quick->len * sizeof(tf_quick_call));

    size_t last_slot =
        quick_program_slot(set, TF_QUICK_PROGRAM_CACHE_WAYS - 1);
    if (ctx->quick_programs[last_slot]) {
        TF_METRIC_INC(ctx, quick_program_cache_evictions);
    }
    quick_program_release(ctx->quick_programs[last_slot]);
    for (size_t way = TF_QUICK_PROGRAM_CACHE_WAYS - 1; way > 0; way--) {
        ctx->quick_programs[quick_program_slot(set, way)] =
            ctx->quick_programs[quick_program_slot(set, way - 1)];
    }
    ctx->quick_programs[first_slot] = quick;
    quick->refcount++;
    return quick;
}

static tf_word *quickened_call_resolve(tf_ctx *ctx, tf_frame *frame,
                                       tf_obj *call, tf_quick_call *cached) {
    TF_METRIC_INC(ctx, quickened_lookup_misses);

    tf_word *word = tf_dict_lookup_from(
        ctx, frame->as.program.package_index, call);
    if (word && cached) {
        cached->generation = ctx->words.resolution_generation;
        cached->entry_index = (size_t)(word - ctx->words.entries);
        cached->kind = TF_QUICK_CALL_WORD;
        if (word->type == TF_WORD_NATIVE) {
            cached->native_impl = word->native_impl;
            cached->kind = TF_QUICK_CALL_NATIVE;
            if (word->native_impl == tf_dup)
                cached->kind = TF_QUICK_CALL_DUP;
            else if (word->native_impl == tf_pred)
                cached->kind = TF_QUICK_CALL_PRED;
            else if (word->native_impl == tf_add)
                cached->kind = TF_QUICK_CALL_ADD;
            else if (word->native_impl == tf_mul)
                cached->kind = TF_QUICK_CALL_MUL;
            else if (word->native_impl == tf_lt)
                cached->kind = TF_QUICK_CALL_LT;
        }
    }
    return word;
}

size_t tf_current_package_index(tf_ctx *ctx) {
    for (size_t i = ctx->call_stack_len; i > 0; i--) {
        tf_frame *frame = &ctx->call_stack[i - 1];
        if (frame_is_program(frame->kind)) {
            return frame->as.program.package_index;
        }
    }
    return TF_ROOT_PACKAGE;
}

static size_t program_package_index(tf_ctx *ctx, tf_obj *program) {
    tf_source_span span = program ? tf_obj_span(program) : (tf_source_span){0};
    if (span.source) {
        return tf_source_file_package(span.source);
    }
    return tf_current_package_index(ctx);
}

static void frame_push_program_kind(tf_ctx *ctx, tf_obj *program,
                                    tf_frame_kind kind, size_t package_index) {
    ensure_call_stack_slot(ctx);
    ctx->call_stack[ctx->call_stack_len].kind = kind;
    ctx->call_stack[ctx->call_stack_len].as.program.program = program;
    ctx->call_stack[ctx->call_stack_len].as.program.pc = 0;
    ctx->call_stack[ctx->call_stack_len].as.program.package_index = package_index;
    ctx->call_stack[ctx->call_stack_len].as.program.quick =
        quick_program_acquire(ctx, program, package_index);
    ctx->call_stack[ctx->call_stack_len].as.program.vars.vars = NULL;
    ctx->call_stack[ctx->call_stack_len].as.program.vars.len = 0;
    ctx->call_stack[ctx->call_stack_len].as.program.vars.cap = 0;
    ctx->call_stack[ctx->call_stack_len].call_site = ctx->current_span;
    tf_obj_retain(program);
    ctx->call_stack_len++;
    TF_METRIC_INC(ctx, program_frames);
    TF_METRIC_MAX(ctx, max_frame_depth, ctx->call_stack_len);
}

void tf_frame_push_program(tf_ctx *ctx, tf_obj *program) {
    frame_push_program_kind(ctx, program, TF_FRAME_PROGRAM,
                            program_package_index(ctx, program));
}

void tf_frame_push_program_package(tf_ctx *ctx, tf_obj *program,
                                   size_t package_index) {
    frame_push_program_kind(ctx, program, TF_FRAME_PROGRAM, package_index);
}

void tf_frame_push_native_handler(tf_ctx *ctx, tf_frame_step_fn step,
                                  tf_frame_cleanup_fn cleanup,
                                  tf_frame_error_fn on_error, void *state) {
    ensure_call_stack_slot(ctx);
    ctx->call_stack[ctx->call_stack_len].kind = TF_FRAME_NATIVE;
    ctx->call_stack[ctx->call_stack_len].as.native.step = step;
    ctx->call_stack[ctx->call_stack_len].as.native.cleanup = cleanup;
    ctx->call_stack[ctx->call_stack_len].as.native.on_error = on_error;
    ctx->call_stack[ctx->call_stack_len].as.native.state = state;
    ctx->call_stack[ctx->call_stack_len].call_site = ctx->current_span;
    ctx->call_stack_len++;
    TF_METRIC_INC(ctx, native_frames);
    TF_METRIC_MAX(ctx, max_frame_depth, ctx->call_stack_len);
}

void tf_frame_push_native(tf_ctx *ctx, tf_frame_step_fn step,
                          tf_frame_cleanup_fn cleanup, void *state) {
    tf_frame_push_native_handler(ctx, step, cleanup, NULL, state);
}

void tf_frame_pop(tf_ctx *ctx, tf_ret status) {
    if (ctx->call_stack_len == 0) return;
    tf_frame *f = &ctx->call_stack[ctx->call_stack_len - 1];

    if (frame_is_program(f->kind)) {
        tf_var *vars = f->as.program.vars.vars
                           ? f->as.program.vars.vars
                           : f->as.program.vars.inline_vars;
        for (size_t i = 0; i < f->as.program.vars.len; i++) {
            tf_obj_release(vars[i].name);
            tf_obj_release(vars[i].val);
        }
        free(f->as.program.vars.vars);
        quick_program_release(f->as.program.quick);
        tf_obj_release(f->as.program.program);
    } else if (f->as.native.cleanup) {
        f->as.native.cleanup(ctx, f->as.native.state, status);
    }

    ctx->call_stack_len--;
}

static void frame_unwind_to(tf_ctx *ctx, size_t entry_depth, tf_ret status) {
    while (ctx->call_stack_len > entry_depth) {
        tf_frame_pop(ctx, status);
    }
}

static tf_ret deferred_call_finish(tf_ctx *ctx, void *state, bool *done) {
    (void)ctx;
    (void)state;
    *done = true;
    return TF_OK;
}

static void deferred_call_cleanup(tf_ctx *ctx, void *state, tf_ret status) {
    (void)state;
    (void)status;
    ctx->deferred_active = false;
}

static tf_ret deferred_call_start_next(tf_ctx *ctx) {
    assert(!ctx->deferred_active);
    assert(ctx->deferred_head);

    tf_deferred_call *call = ctx->deferred_head;
    ctx->deferred_head = call->next;
    if (!ctx->deferred_head) ctx->deferred_tail = NULL;
    ctx->deferred_count--;
    ctx->deferred_active = true;

    tf_obj *callable = call->callable;
    tf_source_span previous_span = ctx->current_span;
    ctx->current_span = tf_obj_span(callable);
    tf_frame_push_native(ctx, deferred_call_finish, deferred_call_cleanup,
                         NULL);

    for (size_t i = 0; i < call->argument_count; i++) {
        tf_stack_push(ctx, call->arguments[i]);
    }

    free(call);
    tf_ret result = tf_vm_call_callable(ctx, callable);
    tf_obj_release(callable);
    ctx->current_span = previous_span;
    return result;
}

static tf_source_span ctx_best_span(tf_ctx *ctx) {
    if (ctx->call_stack_len > 0) {
        tf_frame *f = &ctx->call_stack[ctx->call_stack_len - 1];
        if (f->kind == TF_FRAME_NATIVE && f->call_site.source) {
            return f->call_site;
        }
    }
    if (ctx->current_span.source) return ctx->current_span;
    return (tf_source_span){0};
}

static const char *source_basename(const char *path) {
    if (!path) return "<unknown>";
    const char *name = path;
    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') name = p + 1;
    }
    return name;
}

static void write_stdout(void *userdata, const char *data, size_t length) {
    (void)userdata;
    if (length > 0) fwrite(data, 1, length, stdout);
}

static void write_stderr(void *userdata, const char *data, size_t length) {
    (void)userdata;
    if (length > 0) fwrite(data, 1, length, stderr);
}

void tf_ctx_set_output(tf_ctx *ctx, toy_write_fn output, void *userdata) {
    if (!ctx) return;
    ctx->output = output ? output : write_stdout;
    ctx->output_userdata = output ? userdata : NULL;
    ctx->output_is_console = output == NULL;
}

void tf_ctx_set_diagnostic(tf_ctx *ctx, toy_write_fn diagnostic,
                           void *userdata) {
    if (!ctx) return;
    ctx->diagnostic = diagnostic ? diagnostic : write_stderr;
    ctx->diagnostic_userdata = diagnostic ? userdata : NULL;
    ctx->diagnostic_is_console = diagnostic == NULL;
}

void tf_ctx_write_output(tf_ctx *ctx, const char *data, size_t length) {
    if (!ctx || !data || length == 0) return;
    ctx->output(ctx->output_userdata, data, length);
}

void tf_ctx_write_diagnostic(tf_ctx *ctx, const char *data, size_t length) {
    if (!ctx || !data || length == 0) return;
    ctx->diagnostic(ctx->diagnostic_userdata, data, length);
}

static void ctx_vwrite(toy_write_fn write, void *userdata, const char *fmt,
                       va_list args) {
    char stack_buffer[256];
    va_list count_args;
    va_copy(count_args, args);
    int length = vsnprintf(stack_buffer, sizeof stack_buffer, fmt, count_args);
    va_end(count_args);
    if (length < 0) return;
    if ((size_t)length < sizeof stack_buffer) {
        write(userdata, stack_buffer, (size_t)length);
        return;
    }

    char *buffer = tf_xmalloc((size_t)length + 1);
    va_list write_args;
    va_copy(write_args, args);
    vsnprintf(buffer, (size_t)length + 1, fmt, write_args);
    va_end(write_args);
    write(userdata, buffer, (size_t)length);
    free(buffer);
}

void tf_ctx_outputf(tf_ctx *ctx, const char *fmt, ...) {
    if (!ctx || !fmt) return;
    va_list args;
    va_start(args, fmt);
    ctx_vwrite(ctx->output, ctx->output_userdata, fmt, args);
    va_end(args);
}

void tf_ctx_diagnosticf(tf_ctx *ctx, const char *fmt, ...) {
    if (!ctx || !fmt) return;
    va_list args;
    va_start(args, fmt);
    ctx_vwrite(ctx->diagnostic, ctx->diagnostic_userdata, fmt, args);
    va_end(args);
}

bool tf_ctx_output_is_console(tf_ctx *ctx) {
    return ctx && ctx->output_is_console;
}

static const char *ctx_diagnostic_color(tf_ctx *ctx, const char *color) {
    return ctx->diagnostic_is_console ? tf_terminal_color(color) : "";
}

static void ctx_diagnostic_obj_write(void *userdata, const char *data,
                                     size_t length) {
    tf_ctx_write_diagnostic(userdata, data, length);
}

static void ctx_print_error_stack(tf_ctx *ctx, const char *color) {
    size_t len = tf_stack_len(ctx);
    size_t start = len > TF_ERROR_STACK_LIMIT ? len - TF_ERROR_STACK_LIMIT : 0;

    tf_ctx_diagnosticf(ctx, "  stack %s<%zu>%s",
                       ctx_diagnostic_color(ctx, color), len,
                       ctx_diagnostic_color(ctx, TF_CLR_RESET));
    if (start > 0) tf_ctx_write_diagnostic(ctx, " ...", 4);
    for (size_t i = start; i < len; i++) {
        tf_ctx_write_diagnostic(ctx, " ", 1);
        tf_obj_write_display(tf_stack_peek(ctx, len - 1 - i),
                             ctx_diagnostic_obj_write, ctx, false);
    }
    tf_ctx_write_diagnostic(ctx, "\n", 1);
}

static size_t ctx_program_frame_count(tf_ctx *ctx) {
    size_t program_count = 0;
    size_t frame_count = tf_debug_frame_count(ctx);
    for (size_t depth = 0; depth < frame_count; depth++) {
        tf_debug_frame_info frame;
        if (tf_debug_get_frame(ctx, depth, &frame) &&
            frame.kind == TF_FRAME_PROGRAM) {
            program_count++;
        }
    }
    return program_count;
}

static void ctx_print_error_frames(tf_ctx *ctx) {
    size_t program_count = ctx_program_frame_count(ctx);
    if (program_count <= 1) return;

    size_t printed = 0;
    size_t frame_count = tf_debug_frame_count(ctx);
    for (size_t depth = 0;
         depth < frame_count && printed < TF_ERROR_FRAME_LIMIT; depth++) {
        tf_debug_frame_info frame;
        if (!tf_debug_get_frame(ctx, depth, &frame) ||
            frame.kind != TF_FRAME_PROGRAM) {
            continue;
        }

        tf_source_span location = printed == 0 ? ctx_best_span(ctx)
                                                : frame.location;
        tf_ctx_diagnosticf(ctx, "  in %s",
                           frame.word_name ? frame.word_name : "<program>");
        if (location.source) {
            tf_ctx_diagnosticf(
                ctx, " at %s:%zu:%zu",
                source_basename(tf_source_file_name(location.source)),
                (size_t)location.line, (size_t)location.col);
        }
        tf_ctx_write_diagnostic(ctx, "\n", 1);
        printed++;
    }

    if (program_count > printed) {
        tf_ctx_diagnosticf(ctx, "  ... %zu more frame%s\n",
                           program_count - printed,
                           program_count - printed == 1 ? "" : "s");
    }
}

static void ctx_print_error_context(tf_ctx *ctx, const char* color) {
    ctx_print_error_stack(ctx, color);
    ctx_print_error_frames(ctx);
}

static const char *current_word_name(tf_ctx *ctx) {
    return ctx->current_word ? ctx->current_word : "<native>";
}

void tf_ctx_clear_error(tf_ctx *ctx) {
    if (!ctx) return;
    free(ctx->last_error);
    ctx->last_error = NULL;
}

void tf_ctx_set_error(tf_ctx *ctx, const char *message) {
    if (!ctx) return;
    tf_ctx_clear_error(ctx);
    if (message) ctx->last_error = tf_xstrdup(message);
}

const char *tf_ctx_last_error(tf_ctx *ctx) {
    return ctx ? ctx->last_error : NULL;
}

static void ctx_store_error_vf(tf_ctx *ctx, const char *fmt, va_list args) {
    va_list count_args;
    va_copy(count_args, args);
    int length = vsnprintf(NULL, 0, fmt, count_args);
    va_end(count_args);
    if (length < 0) return;

    char *message = tf_xmalloc((size_t)length + 1);
    va_list write_args;
    va_copy(write_args, args);
    vsnprintf(message, (size_t)length + 1, fmt, write_args);
    va_end(write_args);
    while (length > 0 &&
           (message[length - 1] == '\n' || message[length - 1] == '\r')) {
        message[--length] = '\0';
    }

    tf_ctx_clear_error(ctx);
    ctx->last_error = message;
}

static void ctx_diagnostic_vf(tf_ctx *ctx, const char *label, const char *color,
                              const char *fmt, va_list args) {
    ctx_store_error_vf(ctx, fmt, args);
    tf_ctx_diagnosticf(ctx, "%s%s:%s ", ctx_diagnostic_color(ctx, color),
                       label, ctx_diagnostic_color(ctx, TF_CLR_RESET));
    ctx_vwrite(ctx->diagnostic, ctx->diagnostic_userdata, fmt, args);

    tf_source_span span = ctx_best_span(ctx);
    if (span.source) {
        tf_ctx_diagnosticf(
            ctx, "  at %s:%zu:%zu\n",
            source_basename(tf_source_file_name(span.source)),
            (size_t)span.line, (size_t)span.col);
    }
    ctx_print_error_context(ctx, color);
}

void tf_ctx_parse_error(tf_ctx *ctx, const char *source_name, size_t line,
                        size_t col, const char *message) {
    if (!ctx || !message) return;
    if (ctx->error_suppression_depth > 0) return;
    source_name = source_name ? source_name : "<eval>";

    size_t message_len = strlen(message);
    while (message_len > 0 &&
           (message[message_len - 1] == '\n' ||
            message[message_len - 1] == '\r')) {
        message_len--;
    }

    int length = snprintf(NULL, 0, "%.*s at %s:%zu:%zu", (int)message_len,
                          message, source_name, line, col);
    if (length >= 0) {
        char *stored = tf_xmalloc((size_t)length + 1);
        snprintf(stored, (size_t)length + 1, "%.*s at %s:%zu:%zu",
                 (int)message_len, message, source_name, line, col);
        tf_ctx_set_error(ctx, stored);
        free(stored);
    }

    tf_ctx_diagnosticf(ctx, "%sparsing error:%s %.*s\n  at %s:%zu:%zu\n",
                       ctx_diagnostic_color(ctx, TF_CLR_ERR),
                       ctx_diagnostic_color(ctx, TF_CLR_RESET),
                       (int)message_len, message, source_name, line, col);
    ctx->error_reported = true;
}

void tf_ctx_runtime_errorf(tf_ctx *ctx, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (ctx->error_suppression_depth > 0) {
        ctx_store_error_vf(ctx, fmt, args);
    } else {
        ctx_diagnostic_vf(ctx, "runtime error", TF_CLR_ERR, fmt, args);
    }
    va_end(args);
    ctx->error_reported = true;
}

void tf_ctx_program_errorf(tf_ctx *ctx, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (ctx->error_suppression_depth > 0) {
        ctx_store_error_vf(ctx, fmt, args);
    } else {
        ctx_diagnostic_vf(ctx, "program error", TF_CLR_PROGRAM_ERR, fmt, args);
    }
    va_end(args);
    ctx->error_reported = true;
}

const char *tf_type_name(tf_type type) {
    switch (type) {
    case TF_OBJ_TYPE_BOOL:
        return "bool";
    case TF_OBJ_TYPE_INT:
        return "int";
    case TF_OBJ_TYPE_FLOAT:
        return "float";
    case TF_OBJ_TYPE_STR:
        return "string";
    case TF_OBJ_TYPE_SYMBOL:
        return "symbol";
    case TF_OBJ_TYPE_CALL:
        return "call";
    case TF_OBJ_TYPE_VECTOR:
        return "vector";
    case TF_OBJ_TYPE_LIST:
        return "list";
    case TF_OBJ_TYPE_MAP:
        return "map";
    case TF_OBJ_TYPE_SET:
        return "set";
    case TF_OBJ_TYPE_DEQUE:
        return "deque";
    case TF_OBJ_TYPE_PQUEUE:
        return "pqueue";
    case TF_OBJ_TYPE_RESOURCE:
        return "resource";
    case TF_OBJ_TYPE_VARLIST:
        return "capture list";
    case TF_OBJ_TYPE_VARFETCH:
        return "variable fetch";
    }
    return "unknown";
}

const char *tf_obj_type_name(tf_obj *o) {
    return o ? tf_type_name(tf_obj_typeof(o)) : "missing";
}

bool tf_ctx_stack_error(tf_ctx *ctx, size_t needed) {
    size_t found = tf_stack_len(ctx);

    tf_ctx_runtime_errorf(ctx, "'%s' expected %zu value%s on the stack, found %zu\n",
                          current_word_name(ctx), needed, needed == 1 ? "" : "s",
                          found);
    return false;
}

bool tf_ctx_type_error(tf_ctx *ctx, size_t depth, const char *expected) {
    if (!tf_ctx_require_stack(ctx, depth + 1)) return false;

    tf_obj *o = tf_stack_peek(ctx, depth);

    tf_ctx_runtime_errorf(ctx, "'%s' expected %s at stack depth %zu, found %s\n",
                          current_word_name(ctx), expected, depth,
                          tf_obj_type_name(o));
    return false;
}

static bool frame_handle_error(tf_ctx *ctx, size_t entry_depth,
                               tf_ret status) {
    while (ctx->call_stack_len > entry_depth) {
        tf_frame *f = &ctx->call_stack[ctx->call_stack_len - 1];
        if (f->kind == TF_FRAME_NATIVE && f->as.native.on_error) {
            bool handled = false;
            tf_ret res = f->as.native.on_error(ctx, f->as.native.state, status,
                                               &handled);
            if (res != TF_OK) {
                tf_frame_pop(ctx, res);
                status = res;
                continue;
            }
            if (handled) {
                return true;
            }
        }
        tf_frame_pop(ctx, status);
    }
    return false;
}




/* === Dynamic Capture Scope === */

static bool capture_name_equal(tf_obj *left, tf_obj *right) {
    return left == right ||
           (left->str.len == right->str.len &&
            memcmp(left->str.ptr, right->str.ptr, left->str.len) == 0);
}

static void scope_bind_var(tf_ctx *ctx, tf_obj *name, tf_obj *val) {
    if (ctx->call_stack_len == 0) return;
    tf_frame *f = &ctx->call_stack[ctx->call_stack_len - 1];
    if (!frame_is_program(f->kind)) return;

    // check if variable already exists in current frame and update it
    tf_var_table *vars = &f->as.program.vars;
    tf_var *bindings = vars->vars ? vars->vars : vars->inline_vars;
    for (int i = (int)vars->len - 1; i >= 0; i--) {
        if (capture_name_equal(bindings[i].name, name)) {
            tf_obj_release(bindings[i].val);
            bindings[i].val = val;
            tf_obj_retain(val);
            return;
        }
    }

    // otherwise append new binding
    if (vars->len == TF_CAPTURE_INLINE_CAP && !vars->vars) {
        vars->cap = TF_CAPTURE_INITIAL_CAP;
        vars->vars = tf_xmalloc(sizeof(tf_var) * vars->cap);
        memcpy(vars->vars, vars->inline_vars, sizeof(vars->inline_vars));
        bindings = vars->vars;
    } else if (vars->vars && vars->len >= vars->cap) {
        vars->cap = vars->cap == 0 ? TF_CAPTURE_INITIAL_CAP : vars->cap * 2;
        vars->vars = tf_xrealloc(vars->vars, sizeof(tf_var) * vars->cap);
        bindings = vars->vars;
    }
    bindings[vars->len].name = name;
    bindings[vars->len].val = val;
    tf_obj_retain(name);
    tf_obj_retain(val);
    vars->len++;
}

tf_obj *tf_scope_lookup_var(tf_ctx *ctx, tf_obj *name) {
    for (int i = (int)ctx->call_stack_len - 1; i >= 0; i--) {
        tf_frame *f = &ctx->call_stack[i];
        if (!frame_is_program(f->kind)) continue;
        tf_var_table *vars = &f->as.program.vars;
        tf_var *bindings = vars->vars ? vars->vars : vars->inline_vars;
        for (int j = (int)vars->len - 1; j >= 0; j--) {
            if (capture_name_equal(bindings[j].name, name)) {
                return bindings[j].val;
            }
        }
    }
    return NULL;
}

void tf_ctx_interrupt(tf_ctx *ctx) {
    if (ctx) ctx->interrupted = 1;
}

/*
 * Dispatch a dictionary entry that has already been resolved by tf_dict_lookup().
 * User words schedule program frames; native words run immediately and may
 * schedule continuations before returning.
 */
static tf_ret dict_call_resolved(tf_ctx *ctx, tf_word *word) {
    TF_METRIC_INC(ctx, general_dispatches);
    if (word->type == TF_WORD_USER) {
        TF_METRIC_INC(ctx, user_word_calls);
        frame_push_program_kind(ctx, word->user_impl, TF_FRAME_PROGRAM_USER,
                                word->package_index);
        return TF_OK;
    }
    TF_METRIC_INC(ctx, native_word_calls);
    return word->native_impl(ctx);
}

static bool quick_int_add(int64_t a, int64_t b, int64_t *result) {
    if ((b > 0 && a > INT64_MAX - b) ||
        (b < 0 && a < INT64_MIN - b)) {
        return false;
    }
    *result = a + b;
    return true;
}

static bool quick_int_mul(int64_t a, int64_t b, int64_t *result) {
    if (a > 0) {
        if ((b > 0 && a > INT64_MAX / b) ||
            (b < 0 && b < INT64_MIN / a)) {
            return false;
        }
    } else if (a < 0) {
        if ((b > 0 && a < INT64_MIN / b) ||
            (b < 0 && a < INT64_MAX / b)) {
            return false;
        }
    }
    *result = a * b;
    return true;
}

static tf_ret quick_dup(tf_ctx *ctx) {
    size_t len = ctx->data_stack->vector.len;
    if (len == 0) return tf_dup(ctx);
    tf_obj *value = ctx->data_stack->vector.elem[len - 1];
    tf_vector_push(ctx->data_stack, value);
    TF_METRIC_MAX(ctx, max_data_stack_depth, ctx->data_stack->vector.len);
    tf_obj_retain(value);
    return TF_OK;
}

static tf_ret quick_pred(tf_ctx *ctx) {
    size_t len = ctx->data_stack->vector.len;
    if (len == 0) return tf_pred(ctx);
    tf_obj **top = &ctx->data_stack->vector.elem[len - 1];
    tf_obj *value = *top;
    if (tf_obj_typeof(value) != TF_OBJ_TYPE_INT) return tf_pred(ctx);
    int64_t integer = tf_obj_int_value(value);
    if (integer == INT64_MIN) return tf_pred(ctx);
    *top = tf_obj_new_int(integer - 1);
    tf_obj_release(value);
    return TF_OK;
}

static tf_ret quick_binary_int(tf_ctx *ctx, bool multiply) {
    size_t len = ctx->data_stack->vector.len;
    if (len < 2) return multiply ? tf_mul(ctx) : tf_add(ctx);
    tf_obj **values = ctx->data_stack->vector.elem;
    tf_obj *left = values[len - 2];
    tf_obj *right = values[len - 1];
    if (tf_obj_typeof(left) != TF_OBJ_TYPE_INT ||
        tf_obj_typeof(right) != TF_OBJ_TYPE_INT) {
        return multiply ? tf_mul(ctx) : tf_add(ctx);
    }

    int64_t result = 0;
    int64_t a = tf_obj_int_value(left);
    int64_t b = tf_obj_int_value(right);
    bool ok = multiply ? quick_int_mul(a, b, &result)
                       : quick_int_add(a, b, &result);
    if (!ok) return multiply ? tf_mul(ctx) : tf_add(ctx);

    values[len - 2] = tf_obj_new_int(result);
    ctx->data_stack->vector.len--;
    tf_obj_release(left);
    tf_obj_release(right);
    return TF_OK;
}

static tf_ret quick_lt(tf_ctx *ctx) {
    size_t len = ctx->data_stack->vector.len;
    if (len < 2) return tf_lt(ctx);
    tf_obj **values = ctx->data_stack->vector.elem;
    tf_obj *left = values[len - 2];
    tf_obj *right = values[len - 1];
    if (tf_obj_typeof(left) != TF_OBJ_TYPE_INT ||
        tf_obj_typeof(right) != TF_OBJ_TYPE_INT) {
        return tf_lt(ctx);
    }

    bool result = tf_obj_int_value(left) < tf_obj_int_value(right);
    values[len - 2] = tf_obj_new_bool(result);
    ctx->data_stack->vector.len--;
    tf_obj_release(left);
    tf_obj_release(right);
    return TF_OK;
}

static tf_ret quickened_call_dispatch(tf_ctx *ctx, tf_frame *frame,
                                      tf_obj *instruction, size_t pc) {
    tf_quick_program *quick = frame->as.program.quick;
    tf_quick_call *call = quick && pc < quick->len ? &quick->calls[pc] : NULL;
    /* A validated native target needs no dictionary access. User targets keep
     * dense indexes because dictionary storage can move on a later def. */
    if (call && call->generation == ctx->words.resolution_generation) {
        TF_METRIC_INC(ctx, quickened_lookup_hits);
    } else {
        tf_word *word = quickened_call_resolve(ctx, frame, instruction, call);
        if (!word) {
            tf_ctx_runtime_errorf(ctx, "undefined word '%s'\n",
                                  instruction->str.ptr);
            return TF_ERR;
        }
        if (!call) return dict_call_resolved(ctx, word);
    }

    switch (call->kind) {
    case TF_QUICK_CALL_NATIVE:
        TF_METRIC_INC(ctx, native_word_calls);
        TF_METRIC_INC(ctx, general_dispatches);
        return call->native_impl(ctx);
    case TF_QUICK_CALL_DUP:
        TF_METRIC_INC(ctx, native_word_calls);
        TF_METRIC_INC(ctx, specialized_dispatches);
        return quick_dup(ctx);
    case TF_QUICK_CALL_PRED:
        TF_METRIC_INC(ctx, native_word_calls);
        TF_METRIC_INC(ctx, specialized_dispatches);
        return quick_pred(ctx);
    case TF_QUICK_CALL_ADD:
        TF_METRIC_INC(ctx, native_word_calls);
        TF_METRIC_INC(ctx, specialized_dispatches);
        return quick_binary_int(ctx, false);
    case TF_QUICK_CALL_MUL:
        TF_METRIC_INC(ctx, native_word_calls);
        TF_METRIC_INC(ctx, specialized_dispatches);
        return quick_binary_int(ctx, true);
    case TF_QUICK_CALL_LT:
        TF_METRIC_INC(ctx, native_word_calls);
        TF_METRIC_INC(ctx, specialized_dispatches);
        return quick_lt(ctx);
    case TF_QUICK_CALL_WORD:
        assert(call->entry_index < ctx->words.count);
        return dict_call_resolved(ctx, &ctx->words.entries[call->entry_index]);
    }
    return TF_ERR;
}

/*
 * The main iterative execution engine.
 * Instead of recursive C calls, it uses an explicit `call_stack` of frames.
 * This ensures deep user-defined word recursion does not overflow the C stack.
 */
static tf_ret vm_exec_package(tf_ctx *ctx, tf_obj *program,
                              size_t package_index) {
    tf_ctx_clear_error(ctx);
    ctx->current_span = program ? tf_obj_span(program) : (tf_source_span){0};
    if (tf_obj_typeof(program) != TF_OBJ_TYPE_VECTOR) {
        tf_ctx_runtime_errorf(ctx, "attempted to execute non-vector object\n");
        return TF_ERR;
    }

    /* Run until this invocation's root frame has been popped. Native words
     * that need to resume after user code use TF_FRAME_NATIVE continuations
     * instead of re-entering tf_vm_exec(). */
    size_t entry_depth = ctx->call_stack_len;
    bool debug_continuing = false;
    ctx->error_reported = false;
    frame_push_program_kind(ctx, program, TF_FRAME_PROGRAM_ROOT,
                            package_index);

    while (ctx->call_stack_len > entry_depth) {
        if (ctx->interrupted) {
            frame_unwind_to(ctx, entry_depth, TF_INTERRUPTED);
            ctx->interrupted = 0;  // reset for next run
            return TF_INTERRUPTED;
        }

        if (!ctx->deferred_active && ctx->deferred_head) {
            tf_ret deferred_res = deferred_call_start_next(ctx);
            if (deferred_res == TF_INTERRUPTED ||
                deferred_res == TF_EXIT_REQUESTED) {
                frame_unwind_to(ctx, entry_depth, deferred_res);
                return deferred_res;
            }
            if (deferred_res == TF_ERR) {
                if (!ctx->error_reported) {
                    tf_ctx_runtime_errorf(ctx,
                                          "execution of deferred call failed\n");
                }
                if (frame_handle_error(ctx, entry_depth, TF_ERR)) continue;
                return TF_ERR;
            }
            continue;
        }

        tf_frame *f = &ctx->call_stack[ctx->call_stack_len - 1];
        if (f->kind == TF_FRAME_NATIVE) {
            bool done = false;
            TF_METRIC_INC(ctx, native_continuation_steps);
            tf_ret cont_res =
                f->as.native.step(ctx, f->as.native.state, &done);
            if (cont_res != TF_OK) {
                if (cont_res == TF_ERR) {
                    if (!ctx->error_reported) {
                        tf_ctx_runtime_errorf(ctx, "execution failed\n");
                    }
                    if (frame_handle_error(ctx, entry_depth, cont_res)) continue;
                } else {
                    frame_unwind_to(ctx, entry_depth, cont_res);
                }
                return cont_res;
            }
            if (done) tf_frame_pop(ctx, TF_OK);
            continue;
        }

        if (f->as.program.pc >= f->as.program.program->vector.len) {
            frame_complete(ctx);
            continue;
        }

        size_t pc = f->as.program.pc;
        tf_obj *o = f->as.program.program->vector.elem[pc];
        tf_source_span span = tf_obj_span(o);
        ctx->current_span = span;
        if (!debug_continuing && ctx->debug_hook) {
            tf_debug_event event = {o, span, pc, ctx->call_stack_len};
            tf_debug_action action =
                ctx->debug_hook(ctx, &event, ctx->debug_userdata);
            if (action == TF_DEBUG_ABORT) {
                frame_unwind_to(ctx, entry_depth, TF_INTERRUPTED);
                ctx->interrupted = 0;
                return TF_INTERRUPTED;
            }
            if (action == TF_DEBUG_CONTINUE) debug_continuing = true;
        }
        f->as.program.pc++;
        TF_METRIC_INC(ctx, instructions);
        switch (tf_obj_typeof(o)) {
        case TF_OBJ_TYPE_CALL: {
            TF_METRIC_INC(ctx, call_instructions);
            ctx->current_word = o->str.ptr;
            tf_ret call_res = quickened_call_dispatch(ctx, f, o, pc);
            if (call_res == TF_INTERRUPTED) {
                frame_unwind_to(ctx, entry_depth, TF_INTERRUPTED);
                return TF_INTERRUPTED;
            }
            if (call_res == TF_EXIT_REQUESTED) {
                frame_unwind_to(ctx, entry_depth, TF_EXIT_REQUESTED);
                return TF_EXIT_REQUESTED;
            }
            if (call_res == TF_ERR) {
                if (!ctx->error_reported) {
                    tf_ctx_runtime_errorf(ctx, "execution of word '%s' failed\n",
                                          o->str.ptr);
                }
                // unwind remaining frames
                if (frame_handle_error(ctx, entry_depth, TF_ERR)) {
                    continue;
                }
                return TF_ERR;
            }
            break;
        }
        case TF_OBJ_TYPE_VARLIST:
            for (int i = (int)o->vector.len - 1; i >= 0; i--) {
                tf_obj *val = tf_stack_pop(ctx);
                if (!val) {
                    tf_ctx_runtime_errorf(
                        ctx, "stack underflow during variable binding\n");
                    if (frame_handle_error(ctx, entry_depth, TF_ERR)) {
                        continue;
                    }
                    return TF_ERR;
                }
                scope_bind_var(ctx, o->vector.elem[i], val);
                tf_obj_release(val);
            }
            break;
        case TF_OBJ_TYPE_VARFETCH: {
            tf_obj *val = tf_scope_lookup_var(ctx, o);
            if (!val) {
                tf_ctx_runtime_errorf(ctx, "undefined variable '$%s'\n",
                                      o->str.ptr);
                if (frame_handle_error(ctx, entry_depth, TF_ERR)) {
                    continue;
                }
                return TF_ERR;
            }
            tf_stack_push(ctx, val);
            tf_obj_retain(val);
            break;
        }
        default:
            tf_stack_push(ctx, o);
            tf_obj_retain(o);
            break;
        }
    }
    return TF_OK;
}

tf_ret tf_vm_exec_package(tf_ctx *ctx, tf_obj *program,
                          size_t package_index) {
    tf_obj_pool *previous_pool = tf_obj_pool_enter(&ctx->objects);
    tf_ret result = vm_exec_package(ctx, program, package_index);
    tf_obj_pool_leave(previous_pool);
    return result;
}

tf_ret tf_vm_exec(tf_ctx *ctx, tf_obj *program) {
    return tf_vm_exec_package(ctx, program, TF_ROOT_PACKAGE);
}

tf_ret tf_vm_call_callable(tf_ctx *ctx, tf_obj *callable) {
    tf_type type = tf_obj_typeof(callable);
    if (type == TF_OBJ_TYPE_VECTOR) {
        tf_frame_push_program(ctx, callable);
        return TF_OK;
    }
    if (type == TF_OBJ_TYPE_SYMBOL || type == TF_OBJ_TYPE_CALL) {
        tf_word *word = tf_dict_lookup(ctx, callable);
        if (!word) {
            tf_ctx_runtime_errorf(ctx, "undefined word '%s'\n",
                                  callable->str.ptr);
            return TF_ERR;
        }
        return dict_call_resolved(ctx, word);
    }
    return TF_ERR;
}

static TF_NOINLINE void frame_complete(tf_ctx *ctx) {
    tf_frame *frame = &ctx->call_stack[ctx->call_stack_len - 1];
    if (frame->kind == TF_FRAME_PROGRAM_REPEAT &&
        frame->as.program.vars.cap > 1) {
        frame->as.program.vars.cap--;
        frame->as.program.pc = 0;
        return;
    }
    tf_frame_pop(ctx, TF_OK);
}
#undef TF_NOINLINE

static bool program_binds_captures(tf_obj *program) {
    for (size_t i = 0; i < program->vector.len; i++) {
        if (tf_obj_typeof(program->vector.elem[i]) == TF_OBJ_TYPE_VARLIST) {
            return true;
        }
    }
    return false;
}

bool tf_frame_push_repeat(tf_ctx *ctx, tf_obj *program, int64_t count) {
    if (!ctx || count <= 0 || ctx->debug_hook ||
        (uint64_t)count > (uint64_t)SIZE_MAX ||
        tf_obj_typeof(program) != TF_OBJ_TYPE_VECTOR ||
        program_binds_captures(program)) {
        return false;
    }

    size_t package_index = program_package_index(ctx, program);
    ensure_call_stack_slot(ctx);
    tf_frame *frame = &ctx->call_stack[ctx->call_stack_len];
    frame->kind = TF_FRAME_PROGRAM_REPEAT;
    frame->as.program.program = program;
    frame->as.program.pc = 0;
    frame->as.program.package_index = package_index;
    frame->as.program.quick =
        quick_program_acquire(ctx, program, package_index);
    frame->as.program.vars.vars = NULL;
    frame->as.program.vars.len = 0;
    /* Capture-free repeat frames reuse the otherwise dormant capacity field
     * for their remaining iteration count, keeping every frame the same size. */
    frame->as.program.vars.cap = (size_t)count;
    frame->call_site = ctx->current_span;
    tf_obj_retain(program);
    ctx->call_stack_len++;
    TF_METRIC_INC(ctx, program_frames);
    TF_METRIC_MAX(ctx, max_frame_depth, ctx->call_stack_len);
    return true;
}

#ifdef TF_OBSERVE
bool tf_metrics_write_json(tf_ctx *ctx, FILE *output) {
    if (!ctx || !output) return false;
    tf_runtime_metrics *m = &ctx->metrics;
    int written = fprintf(
        output,
        "{\n"
        "  \"schema\": \"toy.runtime-metrics\",\n"
        "  \"version\": 1,\n"
        "  \"execution\": {\n"
        "    \"instructions\": %" PRIu64 ",\n"
        "    \"call_instructions\": %" PRIu64 ",\n"
        "    \"native_continuation_steps\": %" PRIu64 ",\n"
        "    \"native_word_calls\": %" PRIu64 ",\n"
        "    \"user_word_calls\": %" PRIu64 ",\n"
        "    \"program_frames\": %" PRIu64 ",\n"
        "    \"native_frames\": %" PRIu64 ",\n"
        "    \"max_frame_depth\": %" PRIu64 ",\n"
        "    \"max_data_stack_depth\": %" PRIu64 "\n"
        "  },\n"
        "  \"dispatch\": {\n"
        "    \"dictionary_lookups\": %" PRIu64 ",\n"
        "    \"dictionary_cache_hits\": %" PRIu64 ",\n"
        "    \"dictionary_cache_misses\": %" PRIu64 ",\n"
        "    \"quick_program_cache_hits\": %" PRIu64 ",\n"
        "    \"quick_program_cache_misses\": %" PRIu64 ",\n"
        "    \"quick_program_cache_evictions\": %" PRIu64 ",\n"
        "    \"quickened_lookup_hits\": %" PRIu64 ",\n"
        "    \"quickened_lookup_misses\": %" PRIu64 ",\n"
        "    \"specialized_dispatches\": %" PRIu64 ",\n"
        "    \"general_dispatches\": %" PRIu64 "\n"
        "  }\n"
        "}\n",
        m->instructions, m->call_instructions, m->native_continuation_steps,
        m->native_word_calls, m->user_word_calls, m->program_frames,
        m->native_frames, m->max_frame_depth, m->max_data_stack_depth,
        m->dictionary_lookups, m->dictionary_cache_hits,
        m->dictionary_cache_misses, m->quick_program_cache_hits,
        m->quick_program_cache_misses, m->quick_program_cache_evictions,
        m->quickened_lookup_hits, m->quickened_lookup_misses,
        m->specialized_dispatches, m->general_dispatches);
    return written >= 0 && !ferror(output);
}
#endif

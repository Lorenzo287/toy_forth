#include "tf_repl.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "tf_alloc.h"
#include "tf_terminal.h"
#include "tf_debug_control.h"
#include "tf_docs.h"
#include "tf_exec.h"
#include "tf_parser.h"
#include "tf_obj.h"
#include <linenoise.h>

typedef struct {
    int block_depth;
    int brace_depth;
    int var_depth;
    bool in_string;
    bool escape;
    bool line_comment;
    bool block_comment;
    int list_depth;
} repl_state;

static tf_ret run_source(tf_ctx *ctx, const char *filename, const char *source,
                         bool show_parsed);
static char *read_repl_line(bool complete);
static bool append_text(char **buf, size_t *len, size_t *cap, const char *text);
static void reset_state(repl_state *state);
static void feed_state(repl_state *state, const char *text);
static bool input_complete(const repl_state *state);
static void init_repl_ui(tf_ctx *ctx);
static void free_repl_ui(tf_ctx *ctx);
static void save_history_atexit(void);

static tf_ctx *completion_ctx = NULL;
static char *history_path = NULL;
static bool hints_enabled = true;
static bool trace_enabled = true;
static bool debugger_enabled = false;
static bool debugger_control_initialized = false;
static tf_debug_control debugger_control;
static int hints_color = 90;  // Bright black / Gray
static int history_atexit_registered = 0;

static void repl_message(const char *label, const char *color,
                         const char *format, ...) {
    fprintf(stderr, "%s%s:%s ", tf_terminal_color(color), label,
            tf_terminal_color(TF_CLR_RESET));
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

static void save_history_atexit(void) {
    if (history_path) { linenoiseHistorySave(history_path); }
}

static char *get_history_path(void);
static void repl_completion(const char *buf, linenoiseCompletions *lc);
static char *repl_hints(const char *buf, int *color, int *bold);
static char *doc_stack_hint(const tf_doc_entry *doc);
static tf_ret help_show(tf_ctx *ctx);
static tf_ret hints_toggle(tf_ctx *ctx);
static tf_ret trace_toggle(tf_ctx *ctx);
static tf_ret tdb_toggle(tf_ctx *ctx);
static tf_native_fn repl_command_for_source(tf_ctx *ctx, const char *source);
static void print_word_grid(const char *title, const char **names, size_t count);
static tf_debug_action repl_debug_hook(tf_ctx *ctx,
                                        const tf_debug_event *event,
                                        void *userdata);

#include "generated/tf_repl_builtins.inc"

tf_ret tf_run_file(tf_ctx *ctx, const char *filename, bool show_parsed) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("Failed to open program");
        return TF_ERR;
    }

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    rewind(fp);

    char *prg_text = tf_xmalloc(size + 1);
    size_t n_read = fread(prg_text, 1, size, fp);
    prg_text[n_read] = '\0';
    fclose(fp);

    tf_ret result = run_source(ctx, filename, prg_text, show_parsed);
    free(prg_text);
    return result;
}

static tf_ret hints_toggle(tf_ctx *ctx) {
    (void)ctx;
    hints_enabled = !hints_enabled;
    printf("%shints: %s%s\n", tf_terminal_color(TF_CLR_INFO),
           tf_terminal_color(TF_CLR_RESET), hints_enabled ? "on" : "off");
    return TF_OK;
}

static tf_ret trace_toggle(tf_ctx *ctx) {
    (void)ctx;
    trace_enabled = !trace_enabled;
    printf("%strace: %s%s\n", tf_terminal_color(TF_CLR_INFO),
           tf_terminal_color(TF_CLR_RESET), trace_enabled ? "on" : "off");
    return TF_OK;
}

void tf_tdb_set_enabled(tf_ctx *ctx, bool enabled) {
    if (enabled) {
        if (!debugger_control_initialized) {
            tf_debug_control_init(&debugger_control, false);
            debugger_control_initialized = true;
        } else {
            tf_debug_control_pause(&debugger_control);
        }
    } else if (debugger_control_initialized) {
        tf_debug_control_dispose(&debugger_control);
        debugger_control_initialized = false;
    }
    debugger_enabled = enabled;
    tf_debug_set_hook(ctx, debugger_enabled ? repl_debug_hook : NULL, NULL);
}

static tf_ret tdb_toggle(tf_ctx *ctx) {
    tf_tdb_set_enabled(ctx, !debugger_enabled);
    return TF_OK;
}

static const char *repl_source_basename(const char *path) {
    if (!path) return "<unknown>";
    const char *name = path;
    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') name = p + 1;
    }
    return name;
}

static void debug_print_span(tf_source_span span) {
    if (!span.source) {
        printf("<unknown>");
        return;
    }
    printf("%s:%zu:%zu",
           repl_source_basename(tf_source_file_name(span.source)),
           (size_t)span.line, (size_t)span.col);
}

static void debug_print_stack(tf_ctx *ctx) {
    size_t len = tf_stack_len(ctx);
    printf("%sstack <%zu>%s", tf_terminal_color(TF_CLR_INFO), len,
           tf_terminal_color(TF_CLR_RESET));
    if (len > 0) printf(" ");
    for (size_t i = 0; i < len; i++) {
        tf_obj_print_display_colored(tf_stack_peek(ctx, len - 1 - i));
        if (i + 1 < len) printf(" ");
    }
    printf("\n");
}

static void debug_print_backtrace(tf_ctx *ctx,
                                  const tf_debug_event *event) {
    size_t count = tf_debug_frame_count(ctx);
    for (size_t depth = 0; depth < count; depth++) {
        tf_debug_frame_info frame;
        if (!tf_debug_get_frame(ctx, depth, &frame)) continue;
        printf("  #%zu ", depth);
        if (frame.kind == TF_FRAME_PROGRAM) {
            printf("%s pc %zu/%zu at ",
                   frame.word_name ? frame.word_name : "<program>", frame.pc,
                   frame.program_len);
        } else {
            printf("<native continuation> at ");
        }
        debug_print_span(depth == 0 ? event->span : frame.location);
        printf("\n");
    }
}

static void debug_print_pause(tf_ctx *ctx, const tf_debug_event *event) {
    tf_debug_frame_info frame;
    const char *word_name = "<program>";
    size_t program_len = 0;
    if (tf_debug_get_frame(ctx, 0, &frame) && frame.word_name) {
        word_name = frame.word_name;
        program_len = frame.program_len;
    }
    printf("%spaused:%s ", tf_terminal_color(TF_CLR_INFO),
           tf_terminal_color(TF_CLR_RESET));
    debug_print_span(event->span);
    printf(" in %s (pc %zu/%zu, depth %zu)\n", word_name, event->pc,
           program_len, event->frame_depth);
    printf("  => ");
    tf_obj_print_source_colored(event->instruction);
    printf("\n");
}

static bool parse_positive_size(const char *text, size_t *value) {
    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0 ||
        parsed > SIZE_MAX) {
        return false;
    }
    *value = (size_t)parsed;
    return true;
}

static bool parse_nonnegative_size(const char *text, size_t *value) {
    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > SIZE_MAX) {
        return false;
    }
    *value = (size_t)parsed;
    return true;
}

static void debug_print_locals(tf_ctx *ctx, const char *target) {
    size_t depth = 0;
    while (isspace((unsigned char)*target)) target++;
    if (*target != '\0' && !parse_nonnegative_size(target, &depth)) {
        printf("usage: locals [frame]\n");
        return;
    }

    tf_debug_frame_info frame;
    if (!tf_debug_get_frame(ctx, depth, &frame)) {
        printf("unknown frame %zu\n", depth);
        return;
    }

    size_t count = tf_debug_capture_count(ctx, depth);
    printf("%scaptures for frame %zu (%s)%s\n",
           tf_terminal_color(TF_CLR_INFO), depth,
           frame.word_name ? frame.word_name : "<native continuation>",
           tf_terminal_color(TF_CLR_RESET));
    if (count == 0) {
        printf("  <none>\n");
        return;
    }
    for (size_t i = 0; i < count; i++) {
        tf_debug_capture_info capture;
        if (!tf_debug_get_capture(ctx, depth, i, &capture)) continue;
        printf("  $%s = ", capture.name);
        tf_obj_print_source_colored(capture.value);
        printf("\n");
    }
}

static void debug_print_capture(tf_ctx *ctx, const char *target) {
    while (isspace((unsigned char)*target)) target++;
    if (*target != '$') {
        printf("usage: print $name\n");
        return;
    }
    target++;
    size_t len = strlen(target);
    if (len == 0) {
        printf("usage: print $name\n");
        return;
    }
    for (size_t i = 0; i < len; i++) {
        if (isspace((unsigned char)target[i])) {
            printf("capture names cannot contain whitespace\n");
            return;
        }
    }

    tf_debug_capture_info capture;
    if (!tf_debug_lookup_capture(ctx, target, len, &capture)) {
        printf("unknown capture $%s\n", target);
        return;
    }
    printf("$%s = ", capture.name);
    tf_obj_print_source_colored(capture.value);
    printf("\n");
}

static void debug_print_user_words(tf_ctx *ctx) {
    size_t word_count = tf_debug_word_count(ctx);
    const char **names =
        word_count ? tf_xmalloc(sizeof(*names) * word_count) : NULL;
    size_t user_count = 0;
    for (size_t i = 0; i < word_count; i++) {
        tf_debug_word_info word;
        if (tf_debug_get_word(ctx, i, &word) && word.user_defined) {
            names[user_count++] = word.name;
        }
    }
    if (user_count == 0) {
        printf("no user-defined words\n");
    } else {
        print_word_grid("User words", names, user_count);
    }
    free(names);
}

static void debug_print_definition(tf_ctx *ctx, const char *target) {
    while (isspace((unsigned char)*target)) target++;
    size_t len = strlen(target);
    if (len == 0) {
        printf("usage: see <word>\n");
        return;
    }
    for (size_t i = 0; i < len; i++) {
        if (isspace((unsigned char)target[i])) {
            printf("word names cannot contain whitespace\n");
            return;
        }
    }

    tf_debug_word_info word;
    if (!tf_debug_find_word(ctx, target, len, &word)) {
        printf("unknown word %s\n", target);
    } else if (!word.user_defined) {
        printf("%s is a native word\n", word.name);
    } else {
        printf("'%s ", word.name);
        tf_obj_print_source_colored(word.body);
        printf(" def\n");
    }
}

static void debug_add_breakpoint(const tf_debug_event *event,
                                 const char *target) {
    while (isspace((unsigned char)*target)) target++;
    if (*target == '\0') {
        printf("usage: break <line|word>\n");
        return;
    }

    size_t line = 0;
    size_t id = 0;
    if (parse_positive_size(target, &line)) {
        if (!event->span.source) {
            printf("cannot add a line breakpoint without a source\n");
            return;
        }
        const char *source_name = tf_source_file_name(event->span.source);
        id = tf_debug_control_add_line_breakpoint(
            &debugger_control, source_name, line, event);
        printf("breakpoint %zu: %s:%zu\n", id, source_name, line);
        return;
    }

    for (const char *p = target; *p != '\0'; p++) {
        if (isspace((unsigned char)*p)) {
            printf("word breakpoint names cannot contain whitespace\n");
            return;
        }
    }
    id = tf_debug_control_add_word_breakpoint(&debugger_control, target);
    printf("breakpoint %zu: word %s\n", id, target);
}

static void debug_print_breakpoints(void) {
    size_t count = debugger_control.breakpoint_count;
    if (count == 0) {
        printf("no breakpoints\n");
        return;
    }
    for (size_t i = 0; i < count; i++) {
        const tf_debug_breakpoint *breakpoint =
            tf_debug_control_breakpoint_at(&debugger_control, i);
        if (breakpoint->kind == TF_DEBUG_BREAK_LINE) {
            printf("  %zu  %s:%zu%s\n", breakpoint->id,
                   breakpoint->source_name, breakpoint->line,
                   breakpoint->resolved ? "" : " (pending)");
        } else {
            printf("  %zu  word %s\n", breakpoint->id,
                   breakpoint->word_name);
        }
    }
}

static void debug_remove_breakpoint(const char *target) {
    while (isspace((unsigned char)*target)) target++;
    size_t id = 0;
    if (!parse_positive_size(target, &id)) {
        printf("usage: delete <breakpoint-id>\n");
        return;
    }
    if (tf_debug_control_remove_breakpoint(&debugger_control, id)) {
        printf("deleted breakpoint %zu\n", id);
    } else {
        printf("unknown breakpoint %zu\n", id);
    }
}

static tf_debug_action repl_debug_hook(tf_ctx *ctx,
                                        const tf_debug_event *event,
                                        void *userdata) {
    (void)userdata;
    tf_debug_frame_info frame;
    const char *word_name =
        tf_debug_get_frame(ctx, 0, &frame) ? frame.word_name : NULL;
    tf_debug_stop_reason stop = tf_debug_control_on_event(
        &debugger_control, event, word_name);
    if (stop == TF_DEBUG_STOP_NONE) return TF_DEBUG_STEP;
    if (stop == TF_DEBUG_STOP_BREAKPOINT) {
        printf("%sbreakpoint %zu hit%s\n", tf_terminal_color(TF_CLR_INFO),
               debugger_control.last_breakpoint_id,
               tf_terminal_color(TF_CLR_RESET));
    }
    debug_print_pause(ctx, event);

    while (true) {
        errno = 0;
        char *line = linenoise(TF_CLR_RESET "(tdb) " TF_CLR_RESET);
        if (!line) {
            if (errno == EAGAIN) printf("^C\n");
            return TF_DEBUG_ABORT;
        }

        char *command = line;
        while (isspace((unsigned char)*command)) command++;
        char *end = command + strlen(command);
        while (end > command && isspace((unsigned char)end[-1])) end--;
        *end = '\0';

        if (*command == '\0' || strcmp(command, "s") == 0 ||
            strcmp(command, "step") == 0) {
            tf_debug_control_resume(&debugger_control, TF_DEBUG_RUN_STEP_IN,
                                    event->frame_depth);
            linenoiseFree(line);
            return TF_DEBUG_STEP;
        }
        if (strcmp(command, "n") == 0 || strcmp(command, "next") == 0) {
            tf_debug_control_resume(&debugger_control, TF_DEBUG_RUN_STEP_OVER,
                                    event->frame_depth);
            linenoiseFree(line);
            return TF_DEBUG_STEP;
        }
        if (strcmp(command, "out") == 0 || strcmp(command, "finish") == 0 ||
            strcmp(command, "step-out") == 0) {
            tf_debug_control_resume(&debugger_control, TF_DEBUG_RUN_STEP_OUT,
                                    event->frame_depth);
            linenoiseFree(line);
            return TF_DEBUG_STEP;
        }
        if (strcmp(command, "c") == 0 || strcmp(command, "continue") == 0) {
            tf_debug_control_resume(&debugger_control, TF_DEBUG_RUN_CONTINUE,
                                    event->frame_depth);
            bool has_breakpoints = debugger_control.breakpoint_count > 0;
            linenoiseFree(line);
            return has_breakpoints ? TF_DEBUG_STEP : TF_DEBUG_CONTINUE;
        }
        if (strncmp(command, "break ", 6) == 0) {
            debug_add_breakpoint(event, command + 6);
        } else if (strncmp(command, "b ", 2) == 0) {
            debug_add_breakpoint(event, command + 2);
        } else if (strcmp(command, "break") == 0 ||
                   strcmp(command, "b") == 0) {
            printf("usage: break <line|word>\n");
        } else if (strcmp(command, "breakpoints") == 0 ||
                   strcmp(command, "info break") == 0) {
            debug_print_breakpoints();
        } else if (strncmp(command, "delete ", 7) == 0) {
            debug_remove_breakpoint(command + 7);
        } else if (strncmp(command, "d ", 2) == 0) {
            debug_remove_breakpoint(command + 2);
        } else if (strcmp(command, "clear") == 0) {
            tf_debug_control_clear_breakpoints(&debugger_control);
            printf("cleared breakpoints\n");
        } else if (strcmp(command, "p") == 0 ||
                   strcmp(command, "stack") == 0) {
            debug_print_stack(ctx);
        } else if (strcmp(command, "locals") == 0) {
            debug_print_locals(ctx, "");
        } else if (strncmp(command, "locals ", 7) == 0) {
            debug_print_locals(ctx, command + 7);
        } else if (strncmp(command, "print ", 6) == 0) {
            debug_print_capture(ctx, command + 6);
        } else if (strcmp(command, "print") == 0) {
            printf("usage: print $name\n");
        } else if (strcmp(command, "words") == 0) {
            debug_print_user_words(ctx);
        } else if (strncmp(command, "see ", 4) == 0) {
            debug_print_definition(ctx, command + 4);
        } else if (strcmp(command, "see") == 0) {
            printf("usage: see <word>\n");
        } else if (strcmp(command, "bt") == 0 ||
                   strcmp(command, "backtrace") == 0) {
            debug_print_backtrace(ctx, event);
        } else if (strcmp(command, "off") == 0) {
            tf_tdb_set_enabled(ctx, false);
            linenoiseFree(line);
            return TF_DEBUG_CONTINUE;
        } else if (strcmp(command, "q") == 0 ||
                   strcmp(command, "abort") == 0) {
            linenoiseFree(line);
            return TF_DEBUG_ABORT;
        } else if (strcmp(command, "h") == 0 || strcmp(command, "help") == 0 ||
                   strcmp(command, "?") == 0) {
            printf(
                "  Enter, s, step       step into the next instruction\n"
                "  n, next              step over the next instruction\n"
                "  out, finish          run until the current frame returns\n"
                "  c, continue          run until a breakpoint or completion\n"
                "  b, break LINE|WORD   add a line or word breakpoint\n"
                "  breakpoints          list breakpoints\n"
                "  d, delete ID         delete one breakpoint\n"
                "  clear                delete all breakpoints\n"
                "  p, stack             show the data stack\n"
                "  locals [FRAME]       show captures owned by a frame\n"
                "  print $NAME          show a visible capture\n"
                "  words                list user-defined words\n"
                "  see WORD             show a word definition\n"
                "  bt, backtrace        show VM frames\n"
                "  off                  disable tdb and continue\n"
                "  q, abort             abort the current input\n"
                "  h, help, ?           show this summary\n");
        } else {
            printf("unknown tdb command '%s'; use 'help'\n", command);
        }
        linenoiseFree(line);
    }
}

static int name_ptr_cmp(const void *a, const void *b) {
    const char *const *left = a;
    const char *const *right = b;
    return strcmp(*left, *right);
}

static bool native_word_is_active(tf_ctx *ctx, const tf_builtin_word *builtin) {
    tf_word *word = tf_dict_lookup_scoped(
        ctx, TF_ROOT_PACKAGE, builtin->name, strlen(builtin->name));
    return word && word->type == TF_WORD_NATIVE && word->native_impl == builtin->cb;
}

static tf_native_fn repl_command_for_source(tf_ctx *ctx, const char *source) {
    while (isspace((unsigned char)*source)) source++;
    const char *end = source + strlen(source);
    while (end > source && isspace((unsigned char)end[-1])) end--;
    size_t len = (size_t)(end - source);

    for (size_t i = 0; repl_words[i].name; i++) {
        const tf_builtin_word *builtin = &repl_words[i];
        if (strlen(builtin->name) == len &&
            memcmp(source, builtin->name, len) == 0 &&
            native_word_is_active(ctx, builtin)) {
            return builtin->cb;
        }
    }
    return NULL;
}

static void print_word_grid_formatted(const char *title, const char **names,
                                      size_t count, bool quote_names) {
    if (count == 0) return;

    qsort(names, count, sizeof(*names), name_ptr_cmp);

    size_t longest = 0;
    for (size_t i = 0; i < count; i++) {
        size_t len = strlen(names[i]);
        if (len > longest) longest = len;
    }

    size_t decoration_width = quote_names ? 2 : 0;
    size_t column_width = longest + decoration_width + 2;
    size_t console_width = tf_terminal_width();
    size_t available = console_width > 2 ? console_width - 2 : 1;
    size_t columns = available / column_width;
    if (columns == 0) columns = 1;
    if (columns > count) columns = count;
    size_t rows = (count + columns - 1) / columns;

    printf("%s%s%s\n", tf_terminal_color(TF_CLR_RESET), title,
           tf_terminal_color(TF_CLR_RESET));
    for (size_t row = 0; row < rows; row++) {
        printf("  ");
        for (size_t column = 0; column < columns; column++) {
            size_t index = column * rows + row;
            if (index >= count) continue;

            if (quote_names) printf("\"");
            printf("%s", names[index]);
            if (quote_names) printf("\"");
            size_t next = (column + 1) * rows + row;
            if (next < count) {
                size_t padding = column_width - strlen(names[index]) -
                                 decoration_width;
                printf("%*s", (int)padding, "");
            }
        }
        printf("\n");
    }
    if (strcmp(title, "Packages")) printf("\n");
}

static void print_word_grid(const char *title, const char **names,
                            size_t count) {
    print_word_grid_formatted(title, names, count, false);
}

static void print_core_package_catalog(void) {
    size_t count = 0;
    const tf_package_doc *packages = tf_core_package_docs(&count);
    const char **locators =
        count ? tf_xmalloc(sizeof(*locators) * count) : NULL;
    for (size_t i = 0; i < count; i++) locators[i] = packages[i].locator;
    print_word_grid_formatted("Core packages", locators, count, true);
    free(locators);
}

static void print_builtin_group(tf_ctx *ctx, const tf_builtin_group *group) {
    size_t capacity = 0;
    while (group->words[capacity].name) capacity++;

    const char **names = capacity ? tf_xmalloc(sizeof(*names) * capacity) : NULL;
    size_t count = 0;
    for (size_t i = 0; i < capacity; i++) {
        if (native_word_is_active(ctx, &group->words[i])) {
            names[count++] = group->words[i].name;
        }
    }

    print_word_grid(group->title, names, count);
    free(names);
}

static void print_imported_package(tf_ctx *ctx,
                                   const tf_package_import *imported) {
    const char **names =
        ctx->words.count ? tf_xmalloc(sizeof(*names) * ctx->words.count) : NULL;
    size_t count = 0;
    for (size_t i = 0; i < ctx->words.count; i++) {
        tf_word *word = &ctx->words.entries[i];
        if (word->package_index != imported->target_package_index ||
            !word->is_public) {
            continue;
        }
        size_t len = imported->name_len + 1 + word->name_len;
        char *qualified = tf_xmalloc(len + 1);
        memcpy(qualified, imported->name, imported->name_len);
        qualified[imported->name_len] = '.';
        memcpy(qualified + imported->name_len + 1, word->name,
               word->name_len);
        qualified[len] = '\0';
        names[count++] = qualified;
    }

    print_word_grid(imported->name, names, count);

    for (size_t i = 0; i < count; i++) free((void *)names[i]);
    free(names);
}

static tf_ret help_show(tf_ctx *ctx) {
    size_t group_count = 0;
    const tf_builtin_group *groups = tf_builtin_groups(&group_count);
    for (size_t i = 0; i < group_count; i++) {
        print_builtin_group(ctx, &groups[i]);
        if (strcmp(groups[i].title, "Packages") == 0) {
            print_core_package_catalog();
        }
    }

    const tf_builtin_group repl_group = {"REPL", repl_words};
    print_builtin_group(ctx, &repl_group);

    size_t current_package = tf_current_package_index(ctx);
    for (size_t i = 0; i < ctx->package_imports.len; i++) {
        tf_package_import *imported = &ctx->package_imports.entries[i];
        if (imported->owner_package_index == current_package) {
            print_imported_package(ctx, imported);
        }
    }

    const char **user_names =
        ctx->words.count ? tf_xmalloc(sizeof(*user_names) * ctx->words.count) : NULL;
    size_t user_count = 0;
    for (size_t i = 0; i < ctx->words.count; i++) {
        tf_word *word = &ctx->words.entries[i];
        if (word->package_index == current_package &&
            word->type == TF_WORD_USER) {
            user_names[user_count++] = word->name;
        }
    }
    print_word_grid("User words", user_names, user_count);
    free(user_names);
    return TF_OK;
}

tf_ret tf_run_repl(tf_ctx *ctx, bool show_parsed) {
    char *source = NULL;
    size_t len = 0;
    size_t cap = 0;
    repl_state state;
    bool ctrl_c_exit_armed = false;
    tf_ret repl_result = TF_OK;

    reset_state(&state);
    init_repl_ui(ctx);
    printf("%sToy REPL%s\n", tf_terminal_color(TF_CLR_RESET),
           tf_terminal_color(TF_CLR_RESET));
    printf(
        "%sType 'help' for words; 'hints', 'trace', and 'tdb' toggle REPL "
        "features.%s\n",
        tf_terminal_color(TF_CLR_INFO), tf_terminal_color(TF_CLR_RESET));
#ifdef _WIN32
    printf("%sPress Ctrl-Z to exit.%s\n", tf_terminal_color(TF_CLR_INFO),
           tf_terminal_color(TF_CLR_RESET));
#else
    printf("%sPress Ctrl-D to exit.%s\n", tf_terminal_color(TF_CLR_INFO),
           tf_terminal_color(TF_CLR_RESET));
#endif

    while (1) {
        char *line = read_repl_line(input_complete(&state));
        if (!line) {
            if (errno == EAGAIN) {
                printf("^C\n");
                if (ctrl_c_exit_armed) { break; }

                len = 0;
                if (source) source[0] = '\0';
                reset_state(&state);
                repl_message("interrupt", TF_CLR_WARN,
                             "press Ctrl-C again to exit REPL\n");
                ctrl_c_exit_armed = true;
                continue;
            }
            if (len > 0) {
                fprintf(stderr, "\n%sIncomplete input discarded.%s\n",
                        tf_terminal_color(TF_CLR_ERR),
                        tf_terminal_color(TF_CLR_RESET));
            } else {
                printf("\n");
            }
            break;
        }

        ctrl_c_exit_armed = false;

        if (len == 0 && line[0] == '\0') {
            linenoiseFree(line);
            continue;
        }

        if (line[0] != '\0') { linenoiseHistoryAdd(line); }

        if (!append_text(&source, &len, &cap, line) ||
            !append_text(&source, &len, &cap, "\n")) {
            linenoiseFree(line);
            free(source);
            repl_message("context", TF_CLR_ERR,
                         "failed to grow REPL buffer\n");
            return TF_ERR;
        }

        feed_state(&state, line);
        feed_state(&state, "\n");
        linenoiseFree(line);

        if (!input_complete(&state)) { continue; }

        ctx->suppress_repl_status = false;
        if (debugger_enabled && debugger_control_initialized) {
            tf_debug_control_pause(&debugger_control);
        }
        tf_native_fn repl_command = repl_command_for_source(ctx, source);
        tf_ret result;
        if (repl_command) {
            result = repl_command(ctx);
            ctx->suppress_repl_status = true;
        } else {
            result = run_source(ctx, "<repl>", source, show_parsed);
        }
        if (result == TF_EXIT_REQUESTED) {
            repl_result = result;
            break;
        }
        if (result == TF_ERR || result == TF_INTERRUPTED ||
            (result == TF_OK && ctx->suppress_repl_status)) {
            fflush(stdout);
        } else {
            if (trace_enabled) {
                size_t stack_len = tf_stack_len(ctx);
                printf("%s<%zu>%s", tf_terminal_color(TF_CLR_OK), stack_len,
                       tf_terminal_color(TF_CLR_RESET));
                if (stack_len > 0) printf(" ");
                for (size_t i = 0; i < stack_len; i++) {
                    tf_obj_print_display_colored(tf_stack_peek(ctx, stack_len - 1 - i));
                    if (i < stack_len - 1) printf(" ");
                }
                printf("\n");
            } else {
                printf("%sok%s\n", tf_terminal_color(TF_CLR_OK),
                       tf_terminal_color(TF_CLR_RESET));
            }
        }
        fflush(stdout);

        ctrl_c_exit_armed = false;
        len = 0;
        if (source) source[0] = '\0';
        reset_state(&state);
    }

    free_repl_ui(ctx);
    free(source);
    return repl_result;
}

static tf_ret run_source(tf_ctx *ctx, const char *filename, const char *source,
                         bool show_parsed) {
    tf_obj *prg = tf_parse_source(ctx, filename, source);
    if (!prg) return TF_ERR;

    if (show_parsed) {
        printf("=== Parsed program ===\n");
        size_t count = 0;
        tf_obj_print(prg, &count);
        printf("\n\n");
    }

    tf_ret result = tf_vm_exec(ctx, prg);
    if (result == TF_INTERRUPTED) {
        repl_message("interrupt", TF_CLR_WARN, "execution interrupted\n");
        tf_obj_release(prg);
        ctx->current_span = (tf_source_span){0};
        ctx->current_word = NULL;
        return TF_INTERRUPTED;
    }

    if (show_parsed) {
        printf("\n=== Stack after execution ===\n");
        size_t count = 0;
        tf_obj_print(ctx->data_stack, &count);
        printf("\n");
    }

    tf_obj_release(prg);
    ctx->current_span = (tf_source_span){0};
    ctx->current_word = NULL;
    return result;
}

tf_ret tf_run_string(tf_ctx *ctx, const char *source, bool show_parsed) {
    return run_source(ctx, "<eval>", source, show_parsed);
}

typedef struct {
    const char *buf;
    const char *stem;
    size_t stem_len;
    size_t head_len;
    linenoiseCompletions *completions;
} repl_completion_state;

static void add_word_completion(const char *name, size_t name_len,
                                tf_word *word, void *userdata) {
    (void)word;
    repl_completion_state *state = userdata;
    if (state->stem_len > name_len ||
        memcmp(name, state->stem, state->stem_len) != 0) {
        return;
    }
    char *completion = tf_xmalloc(state->head_len + name_len + 1);
    memcpy(completion, state->buf, state->head_len);
    memcpy(completion + state->head_len, name, name_len);
    completion[state->head_len + name_len] = '\0';
    linenoiseAddCompletion(state->completions, completion);
    free(completion);
}

static void repl_completion(const char *buf, linenoiseCompletions *lc) {
    if (!completion_ctx) return;

    const char *token = buf;

    for (const char *p = buf; *p != '\0'; p++) {
        if (isspace((unsigned char)*p) || *p == '[' || *p == ']' || *p == '|' ||
            *p == '{' || *p == '}' || *p == '(' || *p == ')') {
            token = p + 1;
        }
    }

    if (*token == '\'' || *token == '$') {
        token++;
    }

    repl_completion_state state = {
        buf, token, strlen(token), (size_t)(token - buf), lc,
    };
    tf_dict_each_visible(completion_ctx, add_word_completion, &state);
}

static char *repl_hints(const char *buf, int *color, int *bold) {
    if (!hints_enabled || !completion_ctx) return NULL;

    const char *token = buf;
    for (const char *p = buf; *p != '\0'; p++) {
        if (isspace((unsigned char)*p) || *p == '[' || *p == ']' || *p == '|' ||
            *p == '{' || *p == '}' || *p == '(' || *p == ')') {
            token = p + 1;
        }
    }

    if (*token == '\0') return NULL;

    size_t stem_len = strlen(token);
    tf_obj *sym = tf_obj_new_symbol((char *)token, stem_len);
    tf_word *f = tf_dict_lookup(completion_ctx, sym);
    tf_obj_release(sym);

    tf_doc_entry scratch;
    const tf_doc_entry *doc = tf_word_doc(f, &scratch);
    if (doc && f) {
        *color = hints_color;
        *bold = 0;
        return doc_stack_hint(doc);
    }

    // Also look for user-defined words
    if (f) {
        *color = hints_color;
        *bold = 0;
        return tf_xstrdup("  user word");
    }

    return NULL;
}

static char *doc_stack_hint(const tf_doc_entry *doc) {
    const char *text = doc->stack_effect[0] ? doc->stack_effect : doc->syntax;
    size_t len = strlen(text);

    char *hint = tf_xmalloc(len + 3);
    hint[0] = ' ';
    hint[1] = ' ';
    memcpy(hint + 2, text, len);
    hint[len + 2] = '\0';

    return hint;
}

static char *read_repl_line(bool complete) {
    errno = 0;
    if (!complete) {
        return linenoise(TF_CLR_RESET "...> " TF_CLR_RESET);
    }
    return linenoise(debugger_enabled
                         ? TF_CLR_RESET "toy[tdb]> " TF_CLR_RESET
                         : TF_CLR_RESET "toy> " TF_CLR_RESET);
}

static bool append_text(char **buf, size_t *len, size_t *cap, const char *text) {
    size_t text_len = strlen(text);
    size_t required = *len + text_len + 1;

    if (required > *cap) {
        size_t new_cap = *cap == 0 ? 128 : *cap;
        while (new_cap < required) { new_cap *= 2; }
        *buf = tf_xrealloc(*buf, new_cap);
        *cap = new_cap;
    }

    memcpy(*buf + *len, text, text_len + 1);
    *len += text_len;
    return true;
}

static void reset_state(repl_state *state) {
    memset(state, 0, sizeof(*state));
}

static void feed_state(repl_state *state, const char *text) {
    for (size_t i = 0; text[i] != '\0'; i++) {
        char c = text[i];

        if (state->line_comment) {
            if (c == '\n') state->line_comment = false;
            continue;
        }

        if (state->block_comment) {
            if (c == '*' && text[i + 1] == '/') {
                state->block_comment = false;
                i++;
            }
            continue;
        }

        if (state->in_string) {
            if (state->escape) {
                state->escape = false;
                continue;
            }
            if (c == '\\') {
                state->escape = true;
                continue;
            }
            if (c == '"') state->in_string = false;
            continue;
        }

        if (c == '\\') {
            state->line_comment = true;
            continue;
        }
        if (c == '/' && text[i + 1] == '*') {
            state->block_comment = true;
            i++;
            continue;
        }
        if (c == '(') {
            state->list_depth++;
            continue;
        }
        if (c == ')') {
            if (state->list_depth > 0) state->list_depth--;
            continue;
        }
        if (c == '"') {
            state->in_string = true;
            continue;
        }
        if (c == '[') {
            state->block_depth++;
            continue;
        }
        if (c == ']') {
            if (state->block_depth > 0) state->block_depth--;
            continue;
        }
        if (c == '{') {
            state->brace_depth++;
            continue;
        }
        if (c == '}') {
            if (state->brace_depth > 0) state->brace_depth--;
            continue;
        }
        if (c == '|') {
            if (state->var_depth > 0) {
                state->var_depth--;
            } else {
                state->var_depth++;
            }
            continue;
        }
        if (isspace((unsigned char)c)) continue;
        if (tf_parser_is_symbol_char((unsigned char)c)) continue;
    }
}

static bool input_complete(const repl_state *state) {
    return !state->in_string && !state->escape && !state->line_comment &&
           !state->block_comment && state->list_depth == 0 &&
           state->block_depth == 0 && state->brace_depth == 0 &&
           state->var_depth == 0;
}

static void init_repl_ui(tf_ctx *ctx) {
    completion_ctx = ctx;
    history_path = get_history_path();
    if (debugger_enabled && debugger_control_initialized) {
        tf_debug_control_pause(&debugger_control);
    }
    tf_debug_set_hook(ctx, debugger_enabled ? repl_debug_hook : NULL, NULL);

    if (!history_atexit_registered) {
        atexit(save_history_atexit);
        history_atexit_registered = 1;
    }

    for (size_t i = 0; repl_words[i].name; i++) {
        tf_dict_set_native(ctx, repl_words[i].name, repl_words[i].cb);
    }

    linenoiseSetMultiLine(1);
    linenoiseHistorySetMaxLen(256);
    linenoiseSetCompletionCallback(repl_completion);
    linenoiseSetHintsCallback(repl_hints);
    linenoiseSetFreeHintsCallback(free);
    if (history_path) {
        int history_status = linenoiseHistoryLoad(history_path);
        if (history_status == -1 && errno != ENOENT) {
            repl_message("context", TF_CLR_ERR,
                         "failed to load REPL history\n");
        }
    }
}

static void free_repl_ui(tf_ctx *ctx) {
    tf_tdb_set_enabled(ctx, false);
    if (history_path) {
        if (linenoiseHistorySave(history_path) == -1) {
            repl_message("context", TF_CLR_ERR,
                         "failed to save REPL history\n");
        }
        free(history_path);
        history_path = NULL;
    }
    completion_ctx = NULL;
}

static char *get_history_path(void) {
    const char *home = getenv("HOME");
#ifdef _WIN32
    if (!home) home = getenv("USERPROFILE");
#endif
    if (!home || home[0] == '\0') return NULL;

    const char *name = "/.toy_history";
#ifdef _WIN32
    name = "\\.toy_history";
#endif
    size_t len = strlen(home) + strlen(name) + 1;
    char *path = tf_xmalloc(len);
    snprintf(path, len, "%s%s", home, name);
    return path;
}

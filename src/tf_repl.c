#include "tf_repl.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tf_alloc.h"
#include "tf_console.h"
#include "tf_lexer.h"
#include "tf_lib.h"
#include "tf_obj.h"

#ifdef _WIN32
    #include <conio.h>
    #include <io.h>
    #include <windows.h>
#else
    #include <errno.h>
    #include "linenoise.h"
#endif

typedef struct {
    int block_depth;
    int var_depth;
    int colon_depth;
    bool in_string;
    bool escape;
    bool line_comment;
    bool paren_comment;
    bool token_active;
    char token_first;
    size_t token_len;
} tf_repl_state;

static int run_source(tf_ctx *ctx, char *source, bool debug);
#ifdef _WIN32
static char *read_line(FILE *fp, const char *prompt);
static char *read_console_line(const char *prompt);
#endif
static char *read_repl_line(bool complete);
static bool append_text(char **buf, size_t *len, size_t *cap, const char *text);
static void reset_state(tf_repl_state *state);
static void feed_state(tf_repl_state *state, const char *text);
static bool input_complete(const tf_repl_state *state);
static int is_sym_char(int c);
static void finish_token(tf_repl_state *state);
static void init_repl_ui(tf_ctx *ctx);
static void free_repl_ui(void);
#ifndef _WIN32
static tf_ctx *tf_repl_completion_ctx = NULL;
static char *tf_repl_history_path = NULL;

static char *get_history_path(void);
static void tf_repl_completion(const char *buf, linenoiseCompletions *lc);
#endif

int run_file(tf_ctx *ctx, const char *filename, bool debug) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("Failed to open program");
        return TF_ERR;
    }

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    rewind(fp);

    char *prg_text = xmalloc(size + 1);
    size_t n_read = fread(prg_text, 1, size, fp);
    prg_text[n_read] = '\0';
    fclose(fp);

    int result = run_source(ctx, prg_text, debug);
    free(prg_text);
    return result;
}

int run_repl(tf_ctx *ctx, bool debug) {
    char *source = NULL;
    size_t len = 0;
    size_t cap = 0;
    tf_repl_state state;

    reset_state(&state);
    init_repl_ui(ctx);
    printf("%s=== Toy Forth REPL ===%s\n", tf_console_clr(TF_CLR_PROMPT),
           tf_console_clr(TF_CLR_RESET));
#ifdef _WIN32
    printf("%sPress Ctrl-Z to exit.%s\n",
           tf_console_clr(TF_CLR_INFO), tf_console_clr(TF_CLR_RESET));
#else
    printf("%sPress Ctrl-D to exit.%s\n", tf_console_clr(TF_CLR_INFO),
           tf_console_clr(TF_CLR_RESET));
#endif

    while (1) {
        char *line = read_repl_line(input_complete(&state));
        if (!line) {
            if (len > 0) {
                fprintf(stderr, "\n%sIncomplete input discarded.%s\n",
                        tf_console_clr(TF_CLR_ERR),
                        tf_console_clr(TF_CLR_RESET));
            } else {
                printf("\n");
            }
            break;
        }

        if (len == 0 && line[0] == '\0') {
            free(line);
            continue;
        }

#ifndef _WIN32
        if (line[0] != '\0') { linenoiseHistoryAdd(line); }
#endif

        if (!append_text(&source, &len, &cap, line) ||
            !append_text(&source, &len, &cap, "\n")) {
            free(line);
            free(source);
            tf_console_contextf("failed to grow REPL buffer\n");
            return TF_ERR;
        }
        feed_state(&state, line);
        feed_state(&state, "\n");
        free(line);

        if (!input_complete(&state)) { continue; }

        int result = run_source(ctx, source, debug);
        if (result == TF_ERR) {
            printf("%snot ok%s\n", tf_console_clr(TF_CLR_ERR),
                   tf_console_clr(TF_CLR_RESET));
        } else if (result == TF_INTERRUPTED) {
            fflush(stdout);
        } else {
            printf("%sok%s\n", tf_console_clr(TF_CLR_OK),
                   tf_console_clr(TF_CLR_RESET));
        }
        fflush(stdout);

        len = 0;
        if (source) source[0] = '\0';
        reset_state(&state);
    }

    free_repl_ui();
    free(source);
    return TF_OK;
}

static int run_source(tf_ctx *ctx, char *source, bool debug) {
    tf_obj *prg = lexer(source);
    if (!prg) return TF_ERR;

    if (debug) {
        printf("=== Tokenized program ===\n");
        size_t count = 0;
        print_obj(prg, &count);
        printf("\n\n");
    }

    int result = exec(ctx, prg);
    if (result == TF_INTERRUPTED) {
        tf_console_interruptf("execution interrupted\n");
        release_obj(prg);
        return TF_INTERRUPTED;
    }

    if (result != TF_OK) {
        release_obj(prg);
        return TF_ERR;
    }

    if (debug) {
        printf("\n=== Stack content after execution ===\n");
        size_t count = 0;
        print_obj(ctx->forth_stack, &count);
        printf("\n");
    }

    release_obj(prg);
    return TF_OK;
}

#ifndef _WIN32
static void tf_repl_completion(const char *buf, linenoiseCompletions *lc) {
    if (!tf_repl_completion_ctx) return;

    const char *token = buf;
    const char *prefix = "";
    size_t prefix_len = 0;

    for (const char *p = buf; *p != '\0'; p++) {
        if (isspace((unsigned char)*p) || *p == '[' || *p == ']' || *p == '{' ||
            *p == '}' || *p == '(' || *p == ')') {
            token = p + 1;
        }
    }

    if (*token == '\'' || *token == '$') {
        prefix = token;
        token++;
        prefix_len = 1;
    }

    size_t stem_len = strlen(token);
    size_t head_len = (size_t)(token - buf);

    for (size_t i = 0; i < tf_repl_completion_ctx->functions.capacity; i++) {
        tf_func *func = tf_repl_completion_ctx->functions.buckets[i];
        if (!func) continue;

        if (strncmp(func->name->str.ptr, token, stem_len) != 0) continue;

        size_t word_len = func->name->str.len;
        char *completion = xmalloc(head_len + prefix_len + word_len + 1);
        memcpy(completion, buf, head_len);
        if (prefix_len) memcpy(completion + head_len, prefix, prefix_len);
        memcpy(completion + head_len + prefix_len, func->name->str.ptr,
               word_len);
        completion[head_len + prefix_len + word_len] = '\0';
        linenoiseAddCompletion(lc, completion);
        free(completion);
    }
}
#endif

#ifdef _WIN32
static char *read_line(FILE *fp, const char *prompt) {
    if (_isatty(_fileno(fp))) return read_console_line(prompt);

    size_t cap = 128;
    size_t len = 0;
    char *buf = xmalloc(cap);

    while (1) {
        int c = fgetc(fp);
        if (c == EOF) {
            if (len == 0) {
                free(buf);
                return NULL;
            }
            break;
        }

        if (c == '\r') { continue; }
        if (c == '\n') { break; }

        if (len + 1 >= cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
        buf[len++] = (char)c;
    }

    buf[len] = '\0';
    return buf;
}

static char *read_console_line(const char *prompt) {
    size_t cap = 128;
    size_t len = 0;
    char *buf = xmalloc(cap);

    while (1) {
        int c = _getwch();

        if (c == 0 || c == 0xE0) {
            (void)_getwch();
            continue;
        }

        if (c == 26 && len == 0) {
            free(buf);
            return NULL;
        }
        if (c == '\r') {
            putchar('\n');
            break;
        }
        if (c == '\b') {
            if (len > 0) {
                len--;
                fputs("\b \b", stdout);
                fflush(stdout);
            }
            continue;
        }
        if (c == '\f') {
            tf_clear(NULL);
            printf("%s%s%s", tf_console_clr(TF_CLR_PROMPT), prompt,
                   tf_console_clr(TF_CLR_RESET));
            if (len > 0) { printf("%.*s", (int)len, buf); }
            fflush(stdout);
            continue;
        }
        if (c < 32 || c > 126) { continue; }

        if (len + 1 >= cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
        buf[len++] = (char)c;
        putchar(c);
        fflush(stdout);
    }

    buf[len] = '\0';
    return buf;
}
#endif

static char *read_repl_line(bool complete) {
#ifdef _WIN32
    const char *prompt = complete ? "tf> " : "..> ";
    printf("%s%s%s", tf_console_clr(TF_CLR_PROMPT), prompt,
           tf_console_clr(TF_CLR_RESET));
    fflush(stdout);
    return read_line(stdin, prompt);
#else
    return linenoise(complete ? TF_CLR_PROMPT "tf> " TF_CLR_RESET
                              : TF_CLR_PROMPT "..> " TF_CLR_RESET);
#endif
}

static bool append_text(char **buf, size_t *len, size_t *cap,
                        const char *text) {
    size_t text_len = strlen(text);
    size_t required = *len + text_len + 1;

    if (required > *cap) {
        size_t new_cap = *cap == 0 ? 128 : *cap;
        while (new_cap < required) { new_cap *= 2; }
        *buf = xrealloc(*buf, new_cap);
        *cap = new_cap;
    }

    memcpy(*buf + *len, text, text_len + 1);
    *len += text_len;
    return true;
}

static void reset_state(tf_repl_state *state) {
    memset(state, 0, sizeof(*state));
}

static void feed_state(tf_repl_state *state, const char *text) {
    for (size_t i = 0; text[i] != '\0'; i++) {
        char c = text[i];

        if (state->line_comment) {
            if (c == '\n') state->line_comment = false;
            continue;
        }

        if (state->paren_comment) {
            if (c == ')') state->paren_comment = false;
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
            finish_token(state);
            state->line_comment = true;
            continue;
        }
        if (c == '(') {
            finish_token(state);
            state->paren_comment = true;
            continue;
        }
        if (c == '"') {
            finish_token(state);
            state->in_string = true;
            continue;
        }
        if (c == '[') {
            finish_token(state);
            state->block_depth++;
            continue;
        }
        if (c == ']') {
            finish_token(state);
            if (state->block_depth > 0) state->block_depth--;
            continue;
        }
        if (c == '{') {
            finish_token(state);
            state->var_depth++;
            continue;
        }
        if (c == '}') {
            finish_token(state);
            if (state->var_depth > 0) state->var_depth--;
            continue;
        }
        if (c == ':' || c == ';') {
            finish_token(state);
            state->token_active = true;
            state->token_first = c;
            state->token_len = 1;
            finish_token(state);
            continue;
        }
        if (isspace((unsigned char)c)) {
            finish_token(state);
            continue;
        }
        if (is_sym_char((unsigned char)c)) {
            if (!state->token_active) {
                state->token_active = true;
                state->token_first = c;
                state->token_len = 1;
            } else {
                state->token_len++;
            }
            continue;
        }

        finish_token(state);
    }
}

static bool input_complete(const tf_repl_state *state) {
    return !state->in_string && !state->escape && !state->line_comment &&
           !state->paren_comment && state->block_depth == 0 &&
           state->var_depth == 0 && state->colon_depth == 0;
}

static int is_sym_char(int c) {
    const char *sym_chars = "+-*/%<>=!.";
    return isalpha(c) || isdigit(c) || c == '_' || strchr(sym_chars, c) != NULL;
}

static void init_repl_ui(tf_ctx *ctx) {
#ifdef _WIN32
    (void)ctx;
#endif
#ifndef _WIN32
    tf_repl_completion_ctx = ctx;
    tf_repl_history_path = get_history_path();
    linenoiseSetMultiLine(1);
    linenoiseHistorySetMaxLen(256);
    linenoiseSetCompletionCallback(tf_repl_completion);
    if (tf_repl_history_path) {
        int history_status = linenoiseHistoryLoad(tf_repl_history_path);
        if (history_status == -1 && errno != ENOENT) {
            tf_console_contextf("failed to load REPL history\n");
        }
    }
#endif
}

static void free_repl_ui(void) {
#ifndef _WIN32
    if (tf_repl_history_path) {
        if (linenoiseHistorySave(tf_repl_history_path) == -1) {
            tf_console_contextf("failed to save REPL history\n");
        }
        free(tf_repl_history_path);
        tf_repl_history_path = NULL;
    }
    tf_repl_completion_ctx = NULL;
#endif
}

#ifndef _WIN32
static char *get_history_path(void) {
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') return NULL;

    const char *name = "/.toy_forth_history";
    size_t len = strlen(home) + strlen(name) + 1;
    char *path = xmalloc(len);
    snprintf(path, len, "%s%s", home, name);
    return path;
}
#endif

static void finish_token(tf_repl_state *state) {
    if (!state->token_active) return;

    if (state->token_len == 1) {
        if (state->token_first == ':') {
            state->colon_depth++;
        } else if (state->token_first == ';' && state->colon_depth > 0) {
            state->colon_depth--;
        }
    }

    state->token_active = false;
    state->token_first = '\0';
    state->token_len = 0;
}

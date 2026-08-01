#include "tf_parser.h"
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tf_alloc.h"
#include "tf_terminal.h"
#include "tf_exec.h"

#define TF_TOP_LEVEL_INITIAL_CAP 8

typedef struct {
    tf_source_file *source;
    const char *start;
    const char *pos;
    uint32_t line;
    uint32_t col;
    int error;
    tf_ctx *ctx;
} tf_parser;

static void parser_advance(tf_parser *parser);
static tf_source_span parser_mark(tf_parser *parser);
static void parser_finish_span(tf_parser *parser, tf_source_span *span);

static void skip_spaces(tf_parser *parser) {
    while (isspace((unsigned char)parser->pos[0])) parser_advance(parser);
}

static size_t parser_offset(tf_parser *parser) {
    return (size_t)(parser->pos - parser->start);
}

static void parser_advance(tf_parser *parser) {
    if (parser->pos[0] == '\0') return;
    if (parser->pos[0] == '\n') {
        parser->line++;
        parser->col = 1;
    } else {
        parser->col++;
    }
    parser->pos++;
}

static tf_source_span parser_mark(tf_parser *parser) {
    return (tf_source_span){.source = parser->source,
                            .offset = (uint32_t)parser_offset(parser),
                            .line = parser->line,
                            .col = parser->col,
                            .len = 1};
}

static void parser_finish_span(tf_parser *parser, tf_source_span *span) {
    span->len = (uint32_t)(parser_offset(parser) - span->offset);
}

static const char *source_basename(const char *path) {
    if (!path) return "<unknown>";
    const char *name = path;
    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') name = p + 1;
    }
    return name;
}

static void parser_errorf(tf_parser *parser, const char *fmt, ...) {
    parser->error = 1;
    va_list args;
    va_start(args, fmt);
    va_list count_args;
    va_copy(count_args, args);
    int length = vsnprintf(NULL, 0, fmt, count_args);
    va_end(count_args);
    if (length < 0) {
        va_end(args);
        return;
    }

    char *message = tf_xmalloc((size_t)length + 1);
    vsnprintf(message, (size_t)length + 1, fmt, args);
    va_end(args);

    const char *source_name = tf_source_file_name(parser->source);
    if (parser->ctx) {
        tf_ctx_parse_error(parser->ctx, source_name, (size_t)parser->line,
                           (size_t)parser->col, message);
    } else {
        fprintf(stderr, "%sparsing error:%s %s",
                tf_terminal_color(TF_CLR_ERR),
                tf_terminal_color(TF_CLR_RESET), message);
        fprintf(stderr, "  at %s:%zu:%zu\n", source_basename(source_name),
                (size_t)parser->line, (size_t)parser->col);
    }
    free(message);
}

int tf_parser_is_symbol_char(int c) {
    if (c == '\0') return 0;
    unsigned char uc = (unsigned char)c;
    const char *sym_chars = "+-*%<>=!.?";
    return isalpha(uc) || isdigit(uc) || c == '_' || strchr(sym_chars, uc) != NULL;
}

static tf_obj *parser_tokenize_until(tf_parser *parser, int terminator);
static tf_obj *parser_tokenize_number(tf_parser *parser);
static tf_obj *parser_tokenize_single_char_call(tf_parser *parser);
static tf_obj *parser_tokenize_name(tf_parser *parser);
static tf_obj *parser_tokenize_bare_token(tf_parser *parser);
static tf_obj *parser_tokenize_string(tf_parser *parser);
static tf_obj *parser_vector_to_map(tf_parser *parser, tf_obj *items);
static tf_obj *parser_vector_to_set(tf_parser *parser, tf_obj *items);
static int parser_normalize_capture_names(tf_parser *parser, tf_obj *names);
static int parser_starts_signed_number(tf_parser *parser);
static int parser_at_token_boundary(tf_parser *parser);
static int parser_is_structural_char(int c);
static int parser_skip_block_comment(tf_parser *parser);
static int parser_name_is_valid(tf_parser *parser, const char *name, size_t len);

static int hex_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

tf_obj *tf_parse_source(tf_ctx *ctx, const char *filename,
                        const char *source_text) {
    if (!source_text) return NULL;
    tf_obj_pool *previous_pool =
        ctx ? tf_obj_pool_enter(&ctx->objects) : NULL;
    tf_source_file *source = tf_source_file_new(filename ? filename : "<eval>");
    tf_parser parser_state = {.source = source,
                            .start = source_text,
                            .pos = source_text,
                            .line = 1,
                            .col = 1,
                            .error = 0,
                            .ctx = ctx};
    tf_obj *result = parser_tokenize_until(&parser_state, 0);
    tf_source_file_release(source);
    if (ctx) tf_obj_pool_leave(previous_pool);
    return result;
}

static tf_obj *parser_tokenize_until(tf_parser *parser, int terminator) {
    tf_source_span vector_span = parser_mark(parser);
    tf_obj *prg = terminator == 0
                                  ? tf_obj_new_vector_with_capacity(
                                        TF_TOP_LEVEL_INITIAL_CAP)
                                  : tf_obj_new_vector();

    while (parser->pos && parser->pos[0] != 0) {
        skip_spaces(parser);
        if (*parser->pos == 0) break;  // end of program

        if (parser->pos[0] == '\\') {
            while (parser->pos[0] != '\n' && parser->pos[0] != 0) {
                parser_advance(parser);
            }
            continue;
        }
        if (parser->pos[0] == '/' && parser->pos[1] == '*') {
            if (!parser_skip_block_comment(parser)) {
                tf_obj_release(prg);
                return NULL;
            }
            continue;
        }
        if (terminator && *parser->pos == terminator) {
            parser_advance(parser);
            parser_finish_span(parser, &vector_span);
            tf_obj_set_span(prg, vector_span);
            return prg;
        }

        tf_obj *o = NULL;
        if (!terminator && (parser->pos[0] == ']' || parser->pos[0] == '}' ||
                            parser->pos[0] == ')')) {
            parser_errorf(parser, "unexpected '%c'\n", parser->pos[0]);
        } else if (isdigit((unsigned char)parser->pos[0]) ||
                   parser_starts_signed_number(parser)) {
            o = parser_tokenize_number(parser);
        } else if (parser->pos[0] == '[') {
            tf_source_span span = parser_mark(parser);
            parser_advance(parser);
            o = parser_tokenize_until(parser, ']');
            if (o) {
                parser_finish_span(parser, &span);
                tf_obj_set_span(o, span);
            }
        } else if (parser->pos[0] == '(') {
            tf_source_span span = parser_mark(parser);
            parser_advance(parser);
            tf_obj *items = parser_tokenize_until(parser, ')');
            if (items) {
                o = tf_list_from_vector(items);
                tf_obj_release(items);
                if (o) {
                    parser_finish_span(parser, &span);
                    tf_obj_set_span(o, span);
                }
            }
        } else if (parser->pos[0] == '{') {
            tf_source_span span = parser_mark(parser);
            parser_advance(parser);
            tf_obj *items = parser_tokenize_until(parser, '}');
            if (items) {
                o = parser_vector_to_map(parser, items);
                tf_obj_release(items);
                if (o) {
                    parser_finish_span(parser, &span);
                    tf_obj_set_span(o, span);
                }
            }
        } else if (parser->pos[0] == '#') {
            tf_source_span span = parser_mark(parser);
            parser_advance(parser);
            if (parser->pos[0] != '{') {
                parser_errorf(parser, "expected '{' after '#'\n");
            } else {
                parser_advance(parser);
                tf_obj *items = parser_tokenize_until(parser, '}');
                if (items) {
                    o = parser_vector_to_set(parser, items);
                    tf_obj_release(items);
                    if (o) {
                        parser_finish_span(parser, &span);
                        tf_obj_set_span(o, span);
                    }
                }
            }
        } else if (parser->pos[0] == '|') {
            tf_source_span span = parser_mark(parser);
            parser_advance(parser);
            o = parser_tokenize_until(parser, '|');
            if (o) {
                if (!parser_normalize_capture_names(parser, o)) {
                    tf_obj_release(o);
                    o = NULL;
                } else {
                    o->type = TF_OBJ_TYPE_VARLIST;
                    parser_finish_span(parser, &span);
                    tf_obj_set_span(o, span);
                }
            }
        } else if (parser->pos[0] == '$') {
            tf_source_span span = parser_mark(parser);
            parser_advance(parser);
            o = parser_tokenize_name(parser);
            if (o) {
                if (memchr(o->str.ptr, '.', o->str.len) ||
                    (o->str.len == 1 && o->str.ptr[0] == '/')) {
                    parser_errorf(parser,
                                 "capture names cannot be namespace-qualified\n");
                    tf_obj_release(o);
                    o = NULL;
                } else {
                    o->type = TF_OBJ_TYPE_VARFETCH;
                    parser_finish_span(parser, &span);
                    tf_obj_set_span(o, span);
                }
            } else if (!parser->error) {
                parser_errorf(parser, "expected variable name after '$'\n");
            }
        } else if (parser->pos[0] == '\'') {
            tf_source_span span = parser_mark(parser);
            parser_advance(parser);
            o = parser_tokenize_name(parser);
            if (o) {
                parser_finish_span(parser, &span);
                tf_obj_set_span(o, span);
            }
            if (!o && !parser->error) {
                parser_errorf(parser, "expected symbol name after '\''\n");
            }
        } else if ((parser->pos[0] == '-' || parser->pos[0] == '+') &&
                   (isdigit((unsigned char)parser->pos[1]) ||
                    (parser->pos[1] == '.' &&
                     isdigit((unsigned char)parser->pos[2])))) {
            o = parser_tokenize_single_char_call(parser);
        } else if (parser->pos[0] == '/') {
            o = parser_tokenize_single_char_call(parser);
        } else if (tf_parser_is_symbol_char(parser->pos[0])) {
            o = parser_tokenize_bare_token(parser);
            if (o && o->type == TF_OBJ_TYPE_SYMBOL) {
                o->type = TF_OBJ_TYPE_CALL;
            }
        } else if (parser->pos[0] == '"') {
            o = parser_tokenize_string(parser);
        }

        if (o == NULL) {
            tf_obj_release(prg);
            if (!parser->error) {
                parser_errorf(parser, "unexpected input near '%.16s'\n", parser->pos);
            }
            return NULL;
        }
        tf_vector_push(prg, o);
    }

    if (terminator != 0) {
        tf_obj_release(prg);
        parser_errorf(parser, "expected '%c' but reached end of program\n", terminator);
        return NULL;
    }

    parser_finish_span(parser, &vector_span);
    tf_obj_set_span(prg, vector_span);
    return prg;
}

static int parser_normalize_capture_names(tf_parser *parser, tf_obj *names) {
    for (size_t i = 0; i < names->vector.len; i++) {
        tf_obj *name = names->vector.elem[i];
        if (name->type != TF_OBJ_TYPE_CALL) {
            parser_errorf(parser,
                         "capture list entries must be bare variable names\n");
            return 0;
        }

        for (size_t j = 0; j < i; j++) {
            tf_obj *previous = names->vector.elem[j];
            if (previous->str.len == name->str.len &&
                memcmp(previous->str.ptr, name->str.ptr, name->str.len) == 0) {
                parser_errorf(parser, "duplicate capture name '%s'\n",
                             name->str.ptr);
                return 0;
            }
        }
        if (memchr(name->str.ptr, '.', name->str.len) ||
            (name->str.len == 1 && name->str.ptr[0] == '/')) {
            parser_errorf(parser,
                         "capture names cannot be namespace-qualified\n");
            return 0;
        }
    }
    for (size_t i = 0; i < names->vector.len; i++) {
        names->vector.elem[i]->type = TF_OBJ_TYPE_SYMBOL;
    }
    return 1;
}

#define MAX_NUM_LEN 128
static tf_obj *parser_tokenize_number(tf_parser *parser) {
    char buf[MAX_NUM_LEN];
    const char *start = parser->pos;
    bool flt = false;
    bool digit = false;

    tf_source_span span = parser_mark(parser);
    if (parser->pos[0] == '-' || parser->pos[0] == '+') { parser_advance(parser); }

    while (isdigit((unsigned char)parser->pos[0])) {
        digit = true;
        parser_advance(parser);
    }

    if (parser->pos[0] == '.') {
        flt = true;
        parser_advance(parser);
        while (isdigit((unsigned char)parser->pos[0])) {
            digit = true;
            parser_advance(parser);
        }
    }

    if (!digit) {
        parser_errorf(parser, "malformed number literal\n");
        return NULL;
    }

    if (parser->pos[0] == 'e' || parser->pos[0] == 'E') {
        flt = true;
        parser_advance(parser);
        if (parser->pos[0] == '-' || parser->pos[0] == '+') {
            parser_advance(parser);
        }
        if (!isdigit((unsigned char)parser->pos[0])) {
            parser_errorf(parser, "malformed number literal\n");
            return NULL;
        }
        while (isdigit((unsigned char)parser->pos[0])) parser_advance(parser);
    }
    if (isalpha((unsigned char)parser->pos[0]) || parser->pos[0] == '_' ||
        parser->pos[0] == '.') {
        parser_errorf(parser, "malformed number literal\n");
        return NULL;
    }

    size_t num_len = (size_t)(parser->pos - start);
    if (num_len >= MAX_NUM_LEN) {
        parser_errorf(parser, "number literal is too long\n");
        return NULL;
    }
    memcpy(buf, start, num_len);
    buf[num_len] = 0;
    errno = 0;
    tf_obj *o = NULL;
    if (flt) {
        char *end = NULL;
        double value = strtod(buf, &end);
        if (errno == ERANGE || end != buf + num_len) {
            parser_errorf(parser, "floating-point literal is out of range\n");
            return NULL;
        }
        o = tf_obj_new_float(value);
    } else {
        char *end = NULL;
        int64_t value = strtoll(buf, &end, 10);
        if (errno == ERANGE || end != buf + num_len) {
            parser_errorf(parser, "integer literal is out of range\n");
            return NULL;
        }
        o = tf_obj_new_int_boxed(value);
    }
    parser_finish_span(parser, &span);
    tf_obj_set_span(o, span);
    return o;
}

static tf_obj *parser_tokenize_single_char_call(tf_parser *parser) {
    tf_source_span span = parser_mark(parser);
    tf_obj *o = tf_obj_new_call(parser->pos, 1);
    parser_advance(parser);
    parser_finish_span(parser, &span);
    tf_obj_set_span(o, span);
    return o;
}

static tf_obj *parser_tokenize_name(tf_parser *parser) {
    tf_source_span span = parser_mark(parser);
    const char *start = parser->pos;
    if (parser->pos[0] == '/') {
        parser_advance(parser);
    } else {
        while (tf_parser_is_symbol_char(parser->pos[0])) parser_advance(parser);
    }
    size_t sym_len = (size_t)(parser->pos - start);
    if (sym_len == 0) return NULL;
    if (!parser_name_is_valid(parser, start, sym_len)) return NULL;
    tf_obj *o = tf_obj_new_symbol(start, sym_len);
    parser_finish_span(parser, &span);
    tf_obj_set_span(o, span);
    return o;
}

static int parser_name_is_valid(tf_parser *parser, const char *name, size_t len) {
    if (len == 1 && name[0] == '/') return 1;
    if ((len == 1 && name[0] == '.') ||
        (len == 2 && name[0] == '.' &&
         (name[1] == 's' || name[1] == 'S'))) {
        return 1;
    }
    for (size_t i = 0; i < len; i++) {
        if (name[i] != '.') continue;
        if (i == 0 || i + 1 >= len || name[i + 1] == '.') {
            parser_errorf(parser,
                         "namespace separator '.' must appear between names\n");
            return 0;
        }
    }
    return 1;
}

static tf_obj *parser_tokenize_bare_token(tf_parser *parser) {
    tf_obj *name = parser_tokenize_name(parser);
    if (!name) return NULL;

    bool is_true = name->str.len == 4 && !strncmp(name->str.ptr, "true", 4);
    bool is_false =
        name->str.len == 5 && !strncmp(name->str.ptr, "false", 5);
    if (!is_true && !is_false) return name;

    tf_obj *value = tf_obj_new_bool(is_true);
    tf_obj_set_span(value, name->span);
    tf_obj_release(name);
    return value;
}

static tf_obj *parser_vector_to_map(tf_parser *parser, tf_obj *items) {
    if (items->vector.len % 2 != 0) {
        parser_errorf(parser, "map literal expected key/value pairs\n");
        return NULL;
    }

    tf_obj *map = tf_obj_new_map();
    tf_map_reserve(map, items->vector.len / 2);
    for (size_t i = 0; i < items->vector.len; i += 2) {
        tf_obj *key = items->vector.elem[i];
        tf_obj *value = items->vector.elem[i + 1];
        if (!tf_obj_hashable(key)) {
            parser_errorf(parser, "map literal key is not hashable\n");
            tf_obj_release(map);
            return NULL;
        }
        if (tf_map_has(map, key)) {
            parser_errorf(parser, "map literal contains a duplicate key\n");
            tf_obj_release(map);
            return NULL;
        }
        tf_map_set(map, key, value);
    }
    return map;
}

static tf_obj *parser_vector_to_set(tf_parser *parser, tf_obj *items) {
    tf_obj *set = tf_obj_new_set();
    tf_set_reserve(set, items->vector.len);
    for (size_t i = 0; i < items->vector.len; i++) {
        tf_obj *item = items->vector.elem[i];
        if (!tf_obj_hashable(item)) {
            parser_errorf(parser, "set literal item is not hashable\n");
            tf_obj_release(set);
            return NULL;
        }
        if (tf_set_has(set, item)) {
            parser_errorf(parser, "set literal contains a duplicate item\n");
            tf_obj_release(set);
            return NULL;
        }
        tf_set_add(set, item);
    }
    return set;
}

static tf_obj *parser_tokenize_string(tf_parser *parser) {
    tf_source_span span = parser_mark(parser);
    const char *string_start = parser->pos;
    parser_advance(parser);  // skip opening "
    char inline_buf[TF_STRING_INLINE_CAP + 1];
    size_t cap = sizeof(inline_buf);
    size_t len = 0;
    char *buf = inline_buf;

    while (parser->pos[0] != '"' && parser->pos[0] != 0) {
        if (len + 1 >= cap) {
            size_t new_cap = cap * 2;
            if (buf == inline_buf) {
                buf = tf_xmalloc(new_cap);
                memcpy(buf, inline_buf, len);
            } else {
                buf = tf_xrealloc(buf, new_cap);
            }
            cap = new_cap;
        }

        if (parser->pos[0] == '\\') {
            parser_advance(parser);
            if (parser->pos[0] == 0) break;
            switch (parser->pos[0]) {
            case 'n':
                buf[len++] = '\n';
                break;
            case 'r':
                buf[len++] = '\r';
                break;
            case 't':
                buf[len++] = '\t';
                break;
            case 'x': {
                int high = parser->pos[1] == '\0'
                               ? -1
                               : hex_value((unsigned char)parser->pos[1]);
                int low = parser->pos[1] == '\0' || parser->pos[2] == '\0'
                              ? -1
                              : hex_value((unsigned char)parser->pos[2]);
                if (high < 0 || low < 0) {
                    parser_errorf(
                        parser,
                        "invalid hexadecimal escape; expected \\x followed by two hexadecimal digits\n");
                    if (buf != inline_buf) free(buf);
                    return NULL;
                }
                buf[len++] = (char)((high << 4) | low);
                parser_advance(parser);
                parser_advance(parser);
                break;
            }
            case '"':
                buf[len++] = '"';
                break;
            case '\\':
                buf[len++] = '\\';
                break;
            default:
                parser_errorf(parser, "unknown string escape '\\%c'\n",
                             parser->pos[0]);
                if (buf != inline_buf) free(buf);
                return NULL;
            }
        } else {
            buf[len++] = parser->pos[0];
        }
        parser_advance(parser);
    }

    if (parser->pos[0] != '"') {
        parser->pos = string_start;
        parser->line = span.line;
        parser->col = span.col;
        parser_errorf(parser, "unterminated string literal\n");
        if (buf != inline_buf) free(buf);
        return NULL;
    }
    parser_advance(parser);  // skip closing "

    tf_obj *o = buf == inline_buf ? tf_obj_new_string(buf, len)
                                  : tf_obj_new_string_take(buf, len);
    parser_finish_span(parser, &span);
    tf_obj_set_span(o, span);
    return o;
}

static int parser_starts_signed_number(tf_parser *parser) {
    if ((parser->pos[0] != '-' && parser->pos[0] != '+') ||
        !parser_at_token_boundary(parser)) {
        return 0;
    }
    return isdigit((unsigned char)parser->pos[1]);
}

static int parser_at_token_boundary(tf_parser *parser) {
    if (parser->pos == parser->start) return 1;

    unsigned char prev = (unsigned char)parser->pos[-1];
    if (prev == '/' && parser->pos - parser->start >= 2 &&
        parser->pos[-2] == '*') {
        return 1;
    }
    return isspace(prev) || parser_is_structural_char(prev) || prev == '(' ||
           prev == ')' || prev == '\\';
}

static int parser_is_structural_char(int c) {
    return c == '[' || c == ']' || c == '{' || c == '}' || c == '|' ||
           c == '(' || c == ')';
}

static int parser_skip_block_comment(tf_parser *parser) {
    tf_source_span span = parser_mark(parser);

    parser_advance(parser);
    parser_advance(parser);
    while (parser->pos[0] != '\0') {
        if (parser->pos[0] == '*' && parser->pos[1] == '/') {
            parser_advance(parser);
            parser_advance(parser);
            return 1;
        }
        parser_advance(parser);
    }

    parser->pos = parser->start + span.offset;
    parser->line = span.line;
    parser->col = span.col;
    parser_errorf(parser, "unterminated block comment\n");
    return 0;
}

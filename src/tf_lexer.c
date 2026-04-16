#include "tf_lexer.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tf_alloc.h"

// private helper
static void skip_spaces(tf_lexer *lexer) {
    while (isspace(lexer->pos[0])) lexer->pos++;
}

// private helper
static int is_sym_char(int c) {
    const char *sym_chars = "+-*/%<>=!:;.";
    return isalpha(c) || strchr(sym_chars, c) != NULL;
}

static tf_obj *tokenize_until(tf_lexer *lexer, int terminator);

/* receive a string as input and return a list of tokens,
 * namely a list of tf_obj type objects */
tf_obj *lexer(char *prg_text) {
    tf_lexer lexer_state = {.start = prg_text, .pos = prg_text};
    return tokenize_until(&lexer_state, 0);
}

/* tokenize until terminator, needed to tokenize blocks '[ ... ]'
 * where ']' is the terminator
 *
 * recursive function wrapped by lexer() */
tf_obj *tokenize_until(tf_lexer *lexer, int terminator) {
    tf_obj *prg = init_list_obj();

    while (lexer->pos && lexer->pos[0] != 0) {
        skip_spaces(lexer);
        if (*lexer->pos == 0) break;  // end of program

        if (lexer->pos[0] == '\\') {
            while (lexer->pos[0] != '\n' && lexer->pos[0] != 0) lexer->pos++;
            continue;
        }
        if (lexer->pos[0] == '(') {
            while (lexer->pos[0] != ')' && lexer->pos[0] != 0) lexer->pos++;
			lexer->pos++;
            continue;
        }

		// BASE CASE, we reached ']' terminator
        if (terminator && *lexer->pos == terminator) {
            lexer->pos++;
            return prg;
        }

        tf_obj *o = NULL;
        if (isdigit(lexer->pos[0]) ||
            (lexer->pos[0] == '-' && isdigit(lexer->pos[1]))) {
            o = tokenize_number(lexer);
        } else if (lexer->pos[0] == '[') {
            lexer->pos++;
            o = tokenize_until(lexer, ']');
        } else if (lexer->pos[0] == '\'') {
            lexer->pos++;
            o = tokenize_symbol(lexer);
            if (o && o->type == TF_OBJ_TYPE_SYMBOL) {
                o->str.quoted = true;
            }
        } else if (is_sym_char(lexer->pos[0])) {
            o = tokenize_symbol(lexer);
        } else if (lexer->pos[0] == '"' && lexer->pos[1] != 0) {
            o = tokenize_string(lexer);
        }

        if (o == NULL) {
            release_obj(prg);
            fprintf(stderr, "Syntax error at character %zu: ... %.16s ...\n",
                    lexer->pos - lexer->start, lexer->pos);
            return NULL;
        }
        push_obj(prg, o);
    }

    if (terminator != 0) {
        release_obj(prg);
        fprintf(stderr,
                "Syntax error: expected '%c' but reached end of program\n",
                terminator);
        return NULL;
    }

    return prg;
}

#define MAX_NUM_LEN 128
tf_obj *tokenize_number(tf_lexer *lexer) {
    char buf[MAX_NUM_LEN];
    char *start = lexer->pos;
    bool flt = 0;

    if (lexer->pos[0] == '-') lexer->pos++;
    while ((isdigit(lexer->pos[0]) || lexer->pos[0] == '.')) {
        if (lexer->pos[0] == '.') {
            if (flt) break;  // invalid second dot
            flt = 1;
        }
        lexer->pos++;
    }
    int num_len = lexer->pos - start;
    if (num_len >= MAX_NUM_LEN) return NULL;

    memcpy(buf, start, num_len);
    buf[num_len] = 0;
    return flt ? create_float_obj(atof(buf)) : create_int_obj(atoi(buf));
}

tf_obj *tokenize_symbol(tf_lexer *lexer) {
    char *start = lexer->pos;
    while (is_sym_char(lexer->pos[0])) lexer->pos++;
    int sym_len = lexer->pos - start;
    tf_obj *o = NULL;
    if (!strncmp(start, "true", sym_len))
        o = create_bool_obj(1);
    else if (!strncmp(start, "false", sym_len))
        o = create_bool_obj(0);
    else
        o = create_symbol_obj(start, sym_len);
    return o;
}

tf_obj *tokenize_string(tf_lexer *lexer) {
    lexer->pos++; // skip opening "
    size_t cap = 64;
    size_t len = 0;
    char *buf = xmalloc(cap);

    while (lexer->pos[0] != '"' && lexer->pos[0] != 0) {
        if (len + 1 >= cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }

        if (lexer->pos[0] == '\\') {
            lexer->pos++;
            if (lexer->pos[0] == 0) break;
            switch (lexer->pos[0]) {
            case 'n': buf[len++] = '\n'; break;
            case 'r': buf[len++] = '\r'; break;
            case 't': buf[len++] = '\t'; break;
            case '"': buf[len++] = '"'; break;
            case '\\': buf[len++] = '\\'; break;
            default: buf[len++] = lexer->pos[0]; break;
            }
        } else {
            buf[len++] = lexer->pos[0];
        }
        lexer->pos++;
    }

    if (lexer->pos[0] != '"') {
        free(buf);
        return NULL;
    }
    lexer->pos++; // skip closing "

    tf_obj *o = create_string_obj(buf, len);
    free(buf);
    return o;
}

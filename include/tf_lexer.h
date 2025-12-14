#ifndef TF_LEXER_H
#define TF_LEXER_H

#include "tf_obj.h"

typedef struct {
    char *start;
    char *pos;
} tf_lexer;

tf_obj *lexer(char *prg);
tf_obj *tokenize_number(tf_lexer *lexer);
tf_obj *tokenize_symbol(tf_lexer *lexer);
tf_obj *tokenize_string(tf_lexer *lexer);

#endif  // TF_LEXER_H

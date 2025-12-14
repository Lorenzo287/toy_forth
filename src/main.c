#include <stdio.h>
#include <stdlib.h>
#include "tf_alloc.h"
#include "tf_exec.h"
#include "tf_lexer.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return TF_ERR;
    }

    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        perror("Failed to open program");
        return TF_ERR;
    }

    // get size of file (bytes)
    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    rewind(fp);

    // read file
    char *prg_text = xmalloc(size + 1);
    size_t n_read = fread(prg_text, 1, size, fp);
    prg_text[n_read] = 0;  // add terminator
    fclose(fp);

    // tokenize into program
    tf_obj *prg = lexer(prg_text);
    free(prg_text);
    if (prg) {
        printf("Tokenized program: ");
        print_obj(prg);
        printf("\n");
    } else {
        fprintf(stderr, "Failed to tokenize the program\n");
        return TF_ERR;
    }

    // execute with context (stack and functions)
    tf_ctx *ctx = init_ctx();
    if (ctx && exec(ctx, prg) == TF_OK) {
        printf("Stack content after execution: ");
        print_obj(ctx->stack);
        printf("\n");
    } else {
        fprintf(stderr, "Failed to execute the program\n");
        return TF_ERR;
    }
    return TF_OK;
}

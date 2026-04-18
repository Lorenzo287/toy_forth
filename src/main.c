#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tf_alloc.h"
#include "tf_exec.h"
#include "tf_lexer.h"

typedef struct {
    const char *filename;
    bool debug;
} config;

int parse_args(int argc, char **argv, config *config);

int main(int argc, char **argv) {
    config config = {NULL, false};
    tf_ret ret = parse_args(argc, argv, &config);
    if (ret == TF_ERR) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return ret;
    }

    FILE *fp = fopen(config.filename, "r");
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
        if (config.debug) {
            printf("=== Tokenized program ===\n");
            size_t count = 0;
            print_obj(prg, &count);
            printf("\n\n");
        }
    } else {
        fprintf(stderr, "Failed to tokenize the program\n");
        return TF_ERR;
    }

    // execute with context (stack and functions)
    int result = TF_OK;
    tf_ctx *ctx = init_ctx();
    if (!ctx || exec(ctx, prg) != TF_OK) {
        fprintf(stderr, "Failed to execute the program\n");
        result = TF_ERR;
    } else if (config.debug) {
        printf("\n=== Stack content after execution ===\n");
        size_t count = 0;
        print_obj(ctx->stack, &count);
        printf("\n");
    }
    release_obj(prg);
    free_ctx(ctx);

#ifdef STB_LEAKCHECK
    stb_leakcheck_dumpmem();
#endif
    return result;
}

int parse_args(int argc, char **argv, config *config) {
    if (argc < 2) { return TF_ERR; }

    int i;
    for (i = 0; i < argc; i++)
        if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) {
            config->debug = true;
            break;
        }

    config->filename = config->debug ? (i == 1 ? argv[2] : argv[1]) : argv[1];
    return TF_OK;
}

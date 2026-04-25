#include <stdio.h>
#include <string.h>
#include <signal.h>
#include "tf_alloc.h"
#include "tf_console.h"
#include "tf_exec.h"
#include "tf_repl.h"

typedef struct {
    const char *filename;
    bool debug, help, interactive;
} config;

extern void handle_sigint(int sig);
int parse_args(int argc, char **argv, config *config);

int main(int argc, char **argv) {
    signal(SIGINT, handle_sigint);
    tf_console_init();

    config config = {NULL, false, false, false};
    tf_ret ret = parse_args(argc, argv, &config);
    if (ret == TF_ERR || config.help) {
        fprintf(stderr, "=== Toy Forth Interpreter ===\n");
        fprintf(stderr, "Usage: %s [--debug|-d] [filename]\n", argv[0]);
        fprintf(stderr, "Running without filename starts the REPL\n");
        return ret;
    }

    tf_ctx *ctx = init_ctx();
    if (!ctx) { return TF_ERR; }

    tf_ret result = TF_OK;
    if (config.interactive) {
        result = run_repl(ctx, config.debug);
    } else {
        result = run_file(ctx, config.filename, config.debug);
    }
    free_ctx(ctx);

#ifdef STB_LEAKCHECK
    printf("\n=== stb_leakcheck_dumpmem output ===\n");
    stb_leakcheck_dumpmem();
#endif
    return result;
}

int parse_args(int argc, char **argv, config *config) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) {
            config->debug = true;
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            config->help = true;
        } else if (config->filename == NULL) {
            config->filename = argv[i];
        } else {
            return TF_ERR;
        }
    }
    config->interactive = (config->filename == NULL);
    return TF_OK;
}

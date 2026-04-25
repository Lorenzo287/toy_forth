#ifndef TF_REPL_H
#define TF_REPL_H

#include <stdbool.h>
#include "tf_exec.h"

int run_file(tf_ctx *ctx, const char *filename, bool debug);
int run_repl(tf_ctx *ctx, bool debug);

#endif  // TF_REPL_H

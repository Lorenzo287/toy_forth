#ifndef TF_LIB_H
#define TF_LIB_H

#include "tf_exec.h"

int math_functions(tf_ctx *ctx, char *name);
int stack_functions(tf_ctx *ctx, char *name);
int io_functions(tf_ctx *ctx, char *name);
int compare_functions(tf_ctx *ctx, char *name);
int control_functions(tf_ctx *ctx, char *name);
int definition_functions(tf_ctx *ctx, char *name);

#endif  // TF_LIB_H

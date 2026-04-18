#ifndef TF_LIB_H
#define TF_LIB_H

#include "tf_exec.h"

// Math operations
int tf_add(tf_ctx *ctx);
int tf_sub(tf_ctx *ctx);
int tf_mul(tf_ctx *ctx);
int tf_div(tf_ctx *ctx);
int tf_mod(tf_ctx *ctx);
int tf_abs(tf_ctx *ctx);
int tf_max(tf_ctx *ctx);
int tf_min(tf_ctx *ctx);

// Stack operations
int tf_dup(tf_ctx *ctx);
int tf_drop(tf_ctx *ctx);
int tf_swap(tf_ctx *ctx);
int tf_over(tf_ctx *ctx);
int tf_rot(tf_ctx *ctx);

// I/O operations
int tf_print(tf_ctx *ctx);
int tf_println(tf_ctx *ctx);
int tf_dot(tf_ctx *ctx);
int tf_stack(tf_ctx *ctx);

// Comparison operations
int tf_eq(tf_ctx *ctx);
int tf_ne(tf_ctx *ctx);
int tf_lt(tf_ctx *ctx);
int tf_gt(tf_ctx *ctx);
int tf_le(tf_ctx *ctx);
int tf_ge(tf_ctx *ctx);

// Control operations
int tf_exec(tf_ctx *ctx);
int tf_if_r(tf_ctx *ctx);
int tf_ifelse_r(tf_ctx *ctx);
int tf_times_r(tf_ctx *ctx);
int tf_each_r(tf_ctx *ctx);
int tf_while_r(tf_ctx *ctx);

// Definition operations
int tf_colon(tf_ctx *ctx);
int tf_def(tf_ctx *ctx);

#endif  // TF_LIB_H

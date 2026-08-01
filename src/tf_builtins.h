#ifndef TF_BUILTINS_H
#define TF_BUILTINS_H

#include "tf_exec.h"

/* Native entry points are declared from the same manifest that generates the
   registration tables, documentation, and editor metadata. */
#include "generated/tf_builtins_decls.inc"

/* Helpers shared between builtin implementation groups. */
tf_ret tf_split_string(tf_ctx *ctx);

/* Release continuation storage owned by one interpreter state. */
void tf_control_state_cache_clear(tf_ctx *ctx);

#endif  // TF_BUILTINS_H

#ifndef TF_CONSOLE_H
#define TF_CONSOLE_H

#include <stdbool.h>

#define TF_CLR_RESET "\x1b[0m"
#define TF_CLR_PROMPT "\x1b[96m"
#define TF_CLR_OK "\x1b[92m"
#define TF_CLR_ERR "\x1b[91m"
#define TF_CLR_WARN "\x1b[93m"
#define TF_CLR_INFO "\x1b[90m"

void tf_console_init(void);
bool tf_console_use_color(void);
const char *tf_console_clr(const char *code);
void tf_console_runtime_errorf(const char *fmt, ...);
void tf_console_lexer_errorf(const char *fmt, ...);
void tf_console_interruptf(const char *fmt, ...);
void tf_console_contextf(const char *fmt, ...);

#endif  // TF_CONSOLE_H

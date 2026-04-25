#include "tf_console.h"
#include <stdarg.h>
#include <stdio.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

static bool tf_console_color_enabled = false;

static void tf_console_vmessage(const char *label, const char *color,
                                const char *fmt, va_list args) {
    fprintf(stderr, "%s%s:%s ", tf_console_clr(color), label,
            tf_console_clr(TF_CLR_RESET));
    vfprintf(stderr, fmt, args);
}

#ifdef _WIN32
static bool tf_enable_vt(HANDLE handle) {
    if (handle == INVALID_HANDLE_VALUE || handle == NULL) return false;

    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode)) return false;
    if (!SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        return false;
    }
    return true;
}
#endif

void tf_console_init(void) {
#ifdef _WIN32
    bool out_ok = tf_enable_vt(GetStdHandle(STD_OUTPUT_HANDLE));
    bool err_ok = tf_enable_vt(GetStdHandle(STD_ERROR_HANDLE));
    tf_console_color_enabled = out_ok || err_ok;
#else
    tf_console_color_enabled = isatty(fileno(stdout)) || isatty(fileno(stderr));
#endif
}

bool tf_console_use_color(void) {
    return tf_console_color_enabled;
}

const char *tf_console_clr(const char *code) {
    return tf_console_color_enabled ? code : "";
}

void tf_console_runtime_errorf(const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    tf_console_vmessage("runtime error", TF_CLR_ERR, fmt, args);
    va_end(args);
}

void tf_console_lexer_errorf(const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    tf_console_vmessage("parsing error", TF_CLR_ERR, fmt, args);
    va_end(args);
}

void tf_console_interruptf(const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    tf_console_vmessage("interrupt", TF_CLR_WARN, fmt, args);
    va_end(args);
}

void tf_console_contextf(const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    tf_console_vmessage("context", TF_CLR_ERR, fmt, args);
    va_end(args);
}

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "tf_builtins.h"
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define SETENV _putenv_s
#else
#include <sys/wait.h>
#include <unistd.h>
#define SETENV setenv
#endif
#include "tf_obj.h"
#include "tf_alloc.h"
#include "tf_exec.h"

tf_ret tf_setenv(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 1, TF_OBJ_TYPE_STR) ||
        !tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_STR)) {
        return TF_ERR;
    }

    tf_obj *val = tf_stack_pop(ctx);
    tf_obj *name = tf_stack_pop(ctx);

#ifdef _WIN32
    int res = SETENV(name->str.ptr, val->str.ptr);
    int error_code = res;
#else
    int res = SETENV(name->str.ptr, val->str.ptr, 1);
    int error_code = errno;
#endif

    if (res != 0) {
        tf_ctx_runtime_errorf(ctx, "'set-env' failed for '%s': %s\n",
                              name->str.ptr, strerror(error_code));
        tf_obj_release(val);
        tf_obj_release(name);
        return TF_ERR;
    }

    tf_obj_release(val);
    tf_obj_release(name);
    return TF_OK;
}

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

static int shell_exit_status(int status) {
#ifdef _WIN32
    return status;
#else
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return status;
#endif
}

tf_ret tf_shell(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_STR)) return TF_ERR;

    tf_obj *cmd_obj = tf_stack_pop(ctx);
    FILE *fp = popen(cmd_obj->str.ptr, "r");
    if (!fp) {
        tf_ctx_runtime_errorf(ctx, "'shell' failed to start command\n");
        tf_obj_release(cmd_obj);
        return TF_ERR;
    }

    char *output = NULL;
    size_t total_size = 0;
    size_t output_cap = 0;
    char buffer[1024];

    while (fgets(buffer, sizeof(buffer), fp)) {
        size_t len = strlen(buffer);
        size_t needed = total_size + len + 1;
        if (needed > output_cap) {
            size_t new_cap = output_cap == 0 ? 1024 : output_cap;
            while (new_cap < needed) new_cap *= 2;
            output = tf_xrealloc(output, new_cap);
            output_cap = new_cap;
        }
        memcpy(output + total_size, buffer, len);
        total_size += len;
        output[total_size] = '\0';
    }

    bool read_failed = ferror(fp) != 0;
    int status = pclose(fp);
    if (read_failed || status == -1) {
        free(output);
        tf_obj_release(cmd_obj);
        tf_ctx_runtime_errorf(
            ctx, read_failed ? "'shell' failed while reading command output\n"
                             : "'shell' failed to collect command status\n");
        return TF_ERR;
    }
    tf_stack_push(ctx, tf_obj_new_string_take(output, total_size));
    tf_stack_push(ctx, tf_obj_new_int(shell_exit_status(status)));

    tf_obj_release(cmd_obj);
    return TF_OK;
}

tf_ret tf_argc(tf_ctx *ctx) {
    tf_stack_push(ctx, tf_obj_new_int(ctx->argc > 0 ? ctx->argc : 0));
    return TF_OK;
}

tf_ret tf_argv(tf_ctx *ctx) {
    size_t count = ctx->argc > 0 ? (size_t)ctx->argc : 0;
    tf_obj *argv = tf_obj_new_vector_with_capacity(count);
    for (int i = 0; i < ctx->argc; i++) {
        tf_obj *str = tf_obj_new_string(ctx->argv[i], strlen(ctx->argv[i]));
        tf_vector_push(argv, str);
    }
    tf_stack_push(ctx, argv);
    return TF_OK;
}

tf_ret tf_pwd(tf_ctx *ctx) {
    size_t cap = 256;
    char *buf = tf_xmalloc(cap);
    while (true) {
#ifdef _WIN32
        if (_getcwd(buf, (int)cap)) {
#else
        if (getcwd(buf, cap)) {
#endif
            tf_stack_push(ctx, tf_obj_new_string_take(buf, strlen(buf)));
            return TF_OK;
        }
        if (errno != ERANGE) break;
        cap *= 2;
        buf = tf_xrealloc(buf, cap);
    }
    free(buf);
    tf_ctx_runtime_errorf(ctx, "'pwd' failed to read current directory\n");
    return TF_ERR;
}

tf_ret tf_getenv(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_STR)) return TF_ERR;

    tf_obj *key = tf_stack_pop(ctx);
    char *val = getenv(key->str.ptr);
    if (!val) {
        tf_ctx_runtime_errorf(ctx, "'get-env' variable is not set: %s\n",
                              key->str.ptr);
        tf_obj_release(key);
        return TF_ERR;
    }
    tf_stack_push(ctx, tf_obj_new_string(val, strlen(val)));
    tf_obj_release(key);
    return TF_OK;
}

tf_ret tf_env_q(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_STR)) return TF_ERR;

    tf_obj *key = tf_stack_pop(ctx);
    tf_stack_push(ctx, tf_obj_new_bool(getenv(key->str.ptr) != NULL));
    tf_obj_release(key);
    return TF_OK;
}

tf_ret tf_rand(tf_ctx *ctx) {
    tf_stack_push(ctx, tf_obj_new_int((int64_t)tf_random_u32(&ctx->random)));
    return TF_OK;
}

tf_ret tf_sleep(tf_ctx *ctx) {
    if (!tf_ctx_require_type(ctx, 0, TF_OBJ_TYPE_INT)) return TF_ERR;
    tf_obj *ms_obj = tf_stack_peek(ctx, 0);
    int64_t milliseconds = tf_obj_int_value(ms_obj);
    if (milliseconds < 0) {
        tf_ctx_runtime_errorf(ctx, "'sleep' duration must be non-negative\n");
        return TF_ERR;
    }
    ms_obj = tf_stack_pop(ctx);
    int64_t remaining = milliseconds;
#ifdef _WIN32
    while (remaining > 0) {
        DWORD chunk = remaining > 86400000 ? 86400000 : (DWORD)remaining;
        Sleep(chunk);
        remaining -= chunk;
    }
#else
    while (remaining > 0) {
        int64_t chunk = remaining > 86400000 ? 86400000 : remaining;
        struct timespec req = {.tv_sec = (time_t)(chunk / 1000),
                               .tv_nsec = (long)(chunk % 1000) * 1000000L};
        int sleep_res;
        do {
            sleep_res = nanosleep(&req, &req);
        } while (sleep_res != 0 && errno == EINTR);
        if (sleep_res != 0) {
            tf_obj_release(ms_obj);
            tf_ctx_runtime_errorf(ctx, "'sleep' failed: %s\n", strerror(errno));
            return TF_ERR;
        }
        remaining -= chunk;
    }
#endif
    tf_obj_release(ms_obj);
    return TF_OK;
}

static tf_ret current_calendar_time(tf_ctx *ctx, bool utc) {
    time_t current = time(NULL);
    if (current == (time_t)-1) {
        tf_ctx_runtime_errorf(ctx, "'%s' failed to read the system clock\n",
                              ctx->current_word);
        return TF_ERR;
    }

    struct tm parts;
#ifdef _WIN32
    errno_t res = utc ? gmtime_s(&parts, &current)
                      : localtime_s(&parts, &current);
    if (res != 0) {
#else
    struct tm *res = utc ? gmtime_r(&current, &parts)
                         : localtime_r(&current, &parts);
    if (!res) {
#endif
        tf_ctx_runtime_errorf(ctx, "'%s' failed to convert the current time\n",
                              ctx->current_word);
        return TF_ERR;
    }

    static const struct {
        const char *name;
        size_t len;
    } fields[] = {
        {"year", 4}, {"month", 5}, {"day", 3},
        {"hour", 4}, {"minute", 6}, {"second", 6},
    };
    int64_t values[] = {
        parts.tm_year + 1900,
        parts.tm_mon + 1,
        parts.tm_mday,
        parts.tm_hour,
        parts.tm_min,
        parts.tm_sec,
    };

    tf_obj *result = tf_obj_new_map();
    tf_map_reserve(result, sizeof(fields) / sizeof(fields[0]));
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        tf_obj *key = tf_obj_new_symbol(fields[i].name, fields[i].len);
        tf_obj *value = tf_obj_new_int(values[i]);
        tf_map_set(result, key, value);
        tf_obj_release(key);
        tf_obj_release(value);
    }
    tf_stack_push(ctx, result);
    return TF_OK;
}

tf_ret tf_unix_time(tf_ctx *ctx) {
    time_t current = time(NULL);
    if (current == (time_t)-1) {
        tf_ctx_runtime_errorf(ctx,
                              "'unix-time' failed to read the system clock\n");
        return TF_ERR;
    }
    tf_stack_push(ctx, tf_obj_new_int((int64_t)current));
    return TF_OK;
}

tf_ret tf_local_time(tf_ctx *ctx) {
    return current_calendar_time(ctx, false);
}

tf_ret tf_utc_time(tf_ctx *ctx) {
    return current_calendar_time(ctx, true);
}

tf_ret tf_cpu_time(tf_ctx *ctx) {
    clock_t ticks = clock();
    if (ticks == (clock_t)-1) {
        tf_ctx_runtime_errorf(ctx, "'cpu-time' failed to read process CPU time\n");
        return TF_ERR;
    }
    tf_stack_push(ctx,
                  tf_obj_new_float((double)ticks / (double)CLOCKS_PER_SEC));
    return TF_OK;
}

tf_ret tf_monotonic_ns(tf_ctx *ctx) {
    int64_t nanoseconds = 0;
#ifdef _WIN32
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    if (!QueryPerformanceCounter(&counter) ||
        !QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 ||
        counter.QuadPart < 0) {
        tf_ctx_runtime_errorf(ctx,
                              "'monotonic-ns' failed to read the performance counter\n");
        return TF_ERR;
    }
    int64_t seconds = counter.QuadPart / frequency.QuadPart;
    int64_t remainder = counter.QuadPart % frequency.QuadPart;
    if (seconds > INT64_MAX / 1000000000LL) {
        tf_ctx_runtime_errorf(ctx, "'monotonic-ns' result would overflow\n");
        return TF_ERR;
    }
    int64_t fractional = (int64_t)(
        ((long double)remainder * 1000000000.0L) /
        (long double)frequency.QuadPart);
    nanoseconds = seconds * 1000000000LL + fractional;
#else
    struct timespec current;
    if (clock_gettime(CLOCK_MONOTONIC, &current) != 0 || current.tv_sec < 0 ||
        (uint64_t)current.tv_sec > (uint64_t)INT64_MAX / 1000000000ULL) {
        tf_ctx_runtime_errorf(ctx,
                              "'monotonic-ns' failed to read the monotonic clock\n");
        return TF_ERR;
    }
    nanoseconds = (int64_t)current.tv_sec * 1000000000LL + current.tv_nsec;
#endif
    tf_stack_push(ctx, tf_obj_new_int(nanoseconds));
    return TF_OK;
}

tf_ret tf_exit(tf_ctx *ctx) {
    (void)ctx;
    return TF_EXIT_REQUESTED;
}

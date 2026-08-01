#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif
#include "tf_alloc.h"
#include "tf_terminal.h"
#include "tf_debug_protocol.h"
#include "tf_exec.h"
#include "tf_builtins.h"
#include "tf_repl.h"

typedef struct {
    const char *package_path;
    const char *core_path;
    const char **eval_files;
    size_t eval_file_count;
    size_t eval_file_cap;
    char *eval;
    int script_argc;
    char **script_argv;
    bool show_parsed, tdb, debug_protocol, help, interactive;
} cli_config;

static int parse_args(int argc, char **argv, cli_config *config);
static char *default_core_path(const char *argv0);
static tf_ctx *signal_ctx = NULL;

static void handle_sigint(int sig) {
    (void)sig;
    signal(SIGINT, handle_sigint);
    if (signal_ctx) signal_ctx->interrupted = 1;
}

int main(int argc, char **argv) {
    signal(SIGINT, handle_sigint);

    cli_config config = {0};
    tf_ret ret = parse_args(argc, argv, &config);
    if (ret == TF_ERR || config.help) {
        fprintf(stderr, "=== Toy Interpreter ===\n");
        fprintf(stderr, "Usage: %s [options] [package-directory] [args...]\n",
                argv[0]);
        fprintf(stderr,
                "       %s [options] --file PATH [--file PATH ...] "
                "[args...]\n",
                argv[0]);
        fprintf(stderr, "       %s [options] --eval SOURCE [args...]\n",
                argv[0]);
        fprintf(stderr, "\nRunning without a package starts the REPL\n");
        fprintf(stderr,
                "A package directory must declare 'main package and a public "
                "main word\n");
        fprintf(stderr,
                "--file PATH evaluates a source file outside the package "
                "system; it may be repeated\n");
        fprintf(stderr, "--eval executes program passed as argument\n");
        fprintf(stderr,
                "--core-path PATH overrides the core: package directory\n");
        fprintf(stderr,
                "--parsed shows parsed eval or REPL input and the final stack\n");
        fprintf(stderr,
                "--tdb steps through package, file, eval, or REPL input\n");
        free(config.eval);
        free(config.eval_files);
        return ret;
    }

    tf_terminal_init();

    tf_ctx *ctx = tf_ctx_new(config.script_argc, config.script_argv);
    if (!ctx) return TF_ERR;
    signal_ctx = ctx;

    char *automatic_core_path = default_core_path(argv[0]);
    tf_ctx_set_core_package_path(
        ctx, config.core_path ? config.core_path : automatic_core_path);
    free(automatic_core_path);

    tf_debug_protocol *protocol = NULL;
    if (config.debug_protocol) {
        protocol = tf_debug_protocol_new(stdout, config.eval_files[0]);
        if (!protocol) {
            signal_ctx = NULL;
            tf_ctx_free(ctx);
            return TF_ERR;
        }
        tf_debug_protocol_install(ctx, protocol);
    } else {
        tf_tdb_set_enabled(ctx, config.tdb);
    }

    tf_ret result = TF_OK;

    if (result == TF_OK) {
        if (config.eval != NULL) {
            result = tf_run_string(ctx, config.eval, config.show_parsed);
            free(config.eval);
        } else if (config.eval_file_count > 0) {
            for (size_t i = 0; i < config.eval_file_count && result == TF_OK;
                 i++) {
                result = tf_run_file(ctx, config.eval_files[i],
                                     config.show_parsed);
            }
        } else if (config.interactive) {
            result = tf_run_repl(ctx, config.show_parsed);
        } else {
            result = tf_package_run_main(ctx, config.package_path);
        }
    }
    if (protocol) {
        tf_debug_protocol_install(ctx, NULL);
        tf_debug_protocol_finish(protocol, result);
        tf_debug_protocol_free(protocol);
    } else {
        tf_tdb_set_enabled(ctx, false);
    }
    signal_ctx = NULL;
    tf_ctx_free(ctx);
    free(config.eval_files);

#ifdef TF_ALLOC_STATS
    tf_alloc_stats_dump();
#endif

#ifdef STB_LEAKCHECK
    stb_leakcheck_dumpmem();
#endif
    return result == TF_EXIT_REQUESTED ? 0 : result;
}

static int parse_args(int argc, char **argv, cli_config *config) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--parsed") == 0 || strcmp(argv[i], "-p") == 0) {
            config->show_parsed = true;
        } else if (strcmp(argv[i], "--tdb") == 0) {
            config->tdb = true;
        } else if (strcmp(argv[i], "--debug-protocol") == 0) {
            config->debug_protocol = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            config->help = true;
        } else if (strcmp(argv[i], "--core-path") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--core-path requires an argument\n");
                return TF_ERR;
            }
            config->core_path = argv[++i];
        } else if (strcmp(argv[i], "--file") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--file requires an argument\n");
                return TF_ERR;
            }
            if (config->eval_file_count >= config->eval_file_cap) {
                config->eval_file_cap = config->eval_file_cap
                                            ? config->eval_file_cap * 2
                                            : 2;
                config->eval_files = tf_xrealloc(
                    config->eval_files,
                    sizeof(char *) * config->eval_file_cap);
            }
            config->eval_files[config->eval_file_count++] = argv[++i];
        } else if (strcmp(argv[i], "--eval") == 0 || strcmp(argv[i], "-e") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "-e requires an argument\n");
                return TF_ERR;
            }
            if (config->eval) {
                fprintf(stderr, "--eval may only be specified once\n");
                return TF_ERR;
            }
            config->eval = tf_xmalloc(strlen(argv[i + 1]) + 1);
            strcpy(config->eval, argv[i + 1]);
            i++;  // consume eval argument
        } else {
            bool source_mode = config->eval || config->eval_file_count > 0;
            if (!source_mode) {
                config->package_path = argv[i];
                config->script_argc = argc - i - 1;
                config->script_argv = &argv[i + 1];
            } else {
                config->script_argc = argc - i;
                config->script_argv = &argv[i];
            }
            break;
        }
    }
    int modes = (config->package_path != NULL) + (config->eval != NULL) +
                (config->eval_file_count > 0);
    if (modes > 1) {
        fprintf(stderr,
                "package execution, --eval, and --file are mutually "
                "exclusive\n");
        return TF_ERR;
    }
    config->interactive = modes == 0;
    if (config->debug_protocol &&
        (config->eval_file_count != 1 || config->eval || config->tdb ||
         config->show_parsed || config->package_path)) {
        fprintf(stderr,
                "--debug-protocol requires exactly one --file and cannot "
                "be combined with --parsed, --tdb, --eval, or a package\n");
        return TF_ERR;
    }
    return TF_OK;
}

static char *append_executable_directory(const char *executable,
                                         const char *suffix) {
    const char *slash = strrchr(executable, '/');
    const char *backslash = strrchr(executable, '\\');
    const char *separator = slash;
    if (backslash && (!separator || backslash > separator)) {
        separator = backslash;
    }
    if (!separator) return tf_xstrdup(suffix[0] == '/' ? suffix + 1 : suffix);

    size_t directory_len = (size_t)(separator - executable);
    size_t suffix_len = strlen(suffix);
    char *path = tf_xmalloc(directory_len + suffix_len + 1);
    memcpy(path, executable, directory_len);
    memcpy(path + directory_len, suffix, suffix_len + 1);
    return path;
}

static bool directory_exists(const char *path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

static char *core_directory_for_executable(const char *executable) {
    char *beside_executable = append_executable_directory(executable, "/core");
    if (directory_exists(beside_executable)) return beside_executable;

    char *in_sdk_root = append_executable_directory(executable, "/../core");
    if (directory_exists(in_sdk_root)) {
        free(beside_executable);
        return in_sdk_root;
    }
    free(in_sdk_root);
    return beside_executable;
}

static char *default_core_path(const char *argv0) {
#ifdef _WIN32
    DWORD cap = 512;
    for (;;) {
        char *executable = tf_xmalloc(cap);
        DWORD len = GetModuleFileNameA(NULL, executable, cap);
        if (len > 0 && len < cap) {
            executable[len] = '\0';
            char *path = core_directory_for_executable(executable);
            free(executable);
            return path;
        }
        free(executable);
        if (len == 0 || cap > 32768) break;
        cap *= 2;
    }
#else
#ifdef __APPLE__
    uint32_t cap = 0;
    if (_NSGetExecutablePath(NULL, &cap) == -1 && cap > 0) {
        char *executable = tf_xmalloc(cap);
        if (_NSGetExecutablePath(executable, &cap) == 0) {
            char *resolved = realpath(executable, NULL);
            char *path = core_directory_for_executable(
                resolved ? resolved : executable);
            free(resolved);
            free(executable);
            return path;
        }
        free(executable);
    }
#else
    char executable[4096];
    ssize_t len = readlink("/proc/self/exe", executable,
                           sizeof(executable) - 1);
    if (len > 0) {
        executable[len] = '\0';
        return core_directory_for_executable(executable);
    }
#endif
#endif
    return core_directory_for_executable(argv0);
}

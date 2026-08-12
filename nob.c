#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
/* Nob uses Darwin's non-POSIX _SC_NPROCESSORS_ONLN extension. */
#define _DARWIN_C_SOURCE
#endif
#endif

#define NOBDEF static inline
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "deps/nob/nob.h"

#include <stdint.h>

#ifdef _WIN32
#define TOY_EXE_SUFFIX ".exe"
#define TOY_SHARED_SUFFIX_VALUE ".dll"
#else
#ifdef __APPLE__
#define TOY_SHARED_SUFFIX_VALUE ".dylib"
#else
#define TOY_SHARED_SUFFIX_VALUE ".so"
#endif
#define TOY_EXE_SUFFIX ""
#endif

#include "tools/nob/build.h"

#include "tools/nob/tests.h"

#include "tools/nob/benchmarks.h"

int main(int argc, char **argv) {
    GO_REBUILD_URSELF_PLUS(argc, argv, "deps/nob/nob.h",
                           "tools/nob/build.h",
                           "tools/nob/tests.h",
                           "tools/nob/benchmarks.h");
    const char *program = shift(argv, argc);
    const char *command = NULL;
    const char *benchmark_toy = NULL;
    const char *benchmark_baseline = NULL;
    const char *benchmark_candidate = NULL;
    size_t benchmark_runs = 5;
    size_t benchmark_warmups = 1;
    bool benchmark_runs_set = false;
    bool benchmark_options = false;
    bool comparison_options = false;
    File_Paths benchmark_names = {0};

    Build_Config config = {
        .compiler = COMPILER_GCC,
        .mode = MODE_RELEASE,
        .jobs = (size_t)nprocs(),
        .test_filter = NULL,
    };
    while (argc > 0) {
        const char *argument = shift(argv, argc);
        if (!command && argument[0] != '-') {
            command = argument;
        } else if (command && strcmp(command, "benchmark") == 0 &&
                   argument[0] != '-') {
            da_append(&benchmark_names, argument);
        } else if (strcmp(argument, "--cc") == 0) {
            if (argc == 0 || !parse_compiler(shift(argv, argc),
                                              &config.compiler)) {
                nob_log(ERROR, "--cc requires clang, gcc, msvc, or clang-cl");
                return 1;
            }
        } else if (starts_with(argument, "--cc=")) {
            if (!parse_compiler(argument + strlen("--cc="),
                                &config.compiler)) {
                nob_log(ERROR, "unknown compiler: %s", argument);
                return 1;
            }
        } else if (strcmp(argument, "--mode") == 0) {
            if (argc == 0 || !parse_mode(shift(argv, argc), &config.mode)) {
                nob_log(ERROR,
                        "--mode requires release, debug, alloc, leak, "
                        "observe, or profile");
                return 1;
            }
        } else if (starts_with(argument, "--mode=")) {
            if (!parse_mode(argument + strlen("--mode="), &config.mode)) {
                nob_log(ERROR, "unknown build mode: %s", argument);
                return 1;
            }
        } else if (strcmp(argument, "-j") == 0 ||
                   strcmp(argument, "--jobs") == 0) {
            if (argc == 0 || !parse_count(shift(argv, argc), &config.jobs)) {
                nob_log(ERROR, "%s requires a positive integer", argument);
                return 1;
            }
        } else if (strcmp(argument, "--filter") == 0) {
            if (argc == 0) {
                nob_log(ERROR, "--filter requires text");
                return 1;
            }
            config.test_filter = shift(argv, argc);
        } else if (strcmp(argument, "--runs") == 0) {
            benchmark_options = true;
            benchmark_runs_set = true;
            if (argc == 0 ||
                !parse_count(shift(argv, argc), &benchmark_runs)) {
                nob_log(ERROR, "--runs requires a positive integer");
                return 1;
            }
        } else if (starts_with(argument, "--runs=")) {
            benchmark_options = true;
            benchmark_runs_set = true;
            if (!parse_count(argument + strlen("--runs="),
                             &benchmark_runs)) {
                nob_log(ERROR, "--runs requires a positive integer");
                return 1;
            }
        } else if (strcmp(argument, "--toy") == 0) {
            benchmark_options = true;
            if (argc == 0) {
                nob_log(ERROR, "--toy requires an executable path");
                return 1;
            }
            benchmark_toy = shift(argv, argc);
        } else if (starts_with(argument, "--toy=")) {
            benchmark_options = true;
            benchmark_toy = argument + strlen("--toy=");
            if (benchmark_toy[0] == '\0') {
                nob_log(ERROR, "--toy requires an executable path");
                return 1;
            }
        } else if (strcmp(argument, "--compare") == 0) {
            benchmark_options = true;
            comparison_options = true;
            if (argc < 2) {
                nob_log(ERROR,
                        "--compare requires baseline and candidate executables");
                return 1;
            }
            benchmark_baseline = shift(argv, argc);
            benchmark_candidate = shift(argv, argc);
        } else if (strcmp(argument, "--warmup") == 0) {
            benchmark_options = true;
            comparison_options = true;
            if (argc == 0 ||
                !parse_count(shift(argv, argc), &benchmark_warmups)) {
                nob_log(ERROR, "--warmup requires a positive integer");
                return 1;
            }
        } else if (starts_with(argument, "--warmup=")) {
            benchmark_options = true;
            comparison_options = true;
            if (!parse_count(argument + strlen("--warmup="),
                             &benchmark_warmups)) {
                nob_log(ERROR, "--warmup requires a positive integer");
                return 1;
            }
        } else if (strcmp(argument, "--include") == 0) {
            if (argc == 0) {
                nob_log(ERROR, "--include requires a directory");
                return 1;
            }
            da_append(&config.include_dirs, shift(argv, argc));
        } else if (starts_with(argument, "--include=")) {
            da_append(&config.include_dirs, argument + strlen("--include="));
        } else if (strcmp(argument, "--lib-dir") == 0) {
            if (argc == 0) {
                nob_log(ERROR, "--lib-dir requires a directory");
                return 1;
            }
            da_append(&config.library_dirs, shift(argv, argc));
        } else if (starts_with(argument, "--lib-dir=")) {
            da_append(&config.library_dirs, argument + strlen("--lib-dir="));
        } else if (strcmp(argument, "--lib") == 0) {
            if (argc == 0) {
                nob_log(ERROR, "--lib requires a name or path");
                return 1;
            }
            da_append(&config.libraries, shift(argv, argc));
        } else if (starts_with(argument, "--lib=")) {
            da_append(&config.libraries, argument + strlen("--lib="));
        } else if (strcmp(argument, "-h") == 0 ||
                   strcmp(argument, "--help") == 0) {
            print_usage(program);
            return 0;
        } else {
            nob_log(ERROR, "unknown option: %s", argument);
            print_usage(program);
            return 1;
        }
    }

    if (!command || strcmp(command, "help") == 0 || strcmp(command, "-h") == 0 ||
        strcmp(command, "--help") == 0) {
        print_usage(program);
        return 0;
    }
    if (benchmark_options && strcmp(command, "benchmark") != 0) {
        nob_log(ERROR, "benchmark options require the benchmark command");
        return 1;
    }
    if (benchmark_toy && benchmark_baseline) {
        nob_log(ERROR, "--toy and --compare cannot be used together");
        return 1;
    }
    if (comparison_options && !benchmark_baseline) {
        nob_log(ERROR, "--warmup requires --compare");
        return 1;
    }
    if (strcmp(command, "clean") == 0) {
        Nob_Log_Level previous_level = minimal_log_level;
        minimal_log_level = WARNING;
        bool cleaned = remove_tree("build") && remove_tree("dist");
        minimal_log_level = previous_level;
        if (cleaned) nob_log(INFO, "removed build and dist");
        return cleaned ? 0 : 1;
    }
    if (strcmp(command, "build") != 0 && strcmp(command, "test") != 0 &&
        strcmp(command, "dist") != 0 && strcmp(command, "benchmark") != 0) {
        nob_log(ERROR, "unknown command: %s", command);
        print_usage(program);
        return 1;
    }
    if (strcmp(command, "dist") == 0 &&
        !check_distribution_prerequisites()) {
        return 1;
    }
    bool custom_benchmark = strcmp(command, "benchmark") == 0 &&
                            (benchmark_toy != NULL ||
                             benchmark_baseline != NULL);
    if (!custom_benchmark &&
        !program_on_path(compiler_executable(config.compiler))) {
        nob_log(ERROR, "compiler '%s' was not found on PATH",
                compiler_executable(config.compiler));
        if (config.compiler == COMPILER_MSVC) {
            nob_log(ERROR, "run Nob from a Visual Studio Developer PowerShell");
        }
        return 1;
    }
#ifdef _WIN32
    if (config.mode == MODE_PROFILE && config.compiler == COMPILER_GCC) {
        nob_log(ERROR, "profile mode with MinGW is not supported");
        return 1;
    }
#endif
    if (!custom_benchmark && !configure_paths(&config)) return 1;

    if (!custom_benchmark) {
        nob_log(INFO, "compiler: %s", compiler_name(config.compiler));
        nob_log(INFO, "mode: %s", mode_name(config.mode));
        nob_log(INFO, "jobs: %zu", config.jobs);
    }

    const char *root = get_current_dir_temp();
    Compile_Commands compile_commands = {0};
    bool needs_core = strcmp(command, "build") == 0 ||
                      strcmp(command, "test") == 0 ||
                      strcmp(command, "dist") == 0 ||
                      (strcmp(command, "benchmark") == 0 &&
                       !custom_benchmark);
    bool ok = !needs_core || build_core(&config, &compile_commands);
    if (ok && strcmp(command, "test") == 0) {
        ok = run_all_tests(&config, root, &compile_commands);
    }
    if (ok && strcmp(command, "dist") == 0) {
        ok = build_distribution(&config, root);
    }
    if (ok && strcmp(command, "benchmark") == 0) {
        if (benchmark_baseline) {
            size_t pairs = benchmark_runs_set ? benchmark_runs : 15;
            ok = compare_benchmarks(benchmark_baseline, benchmark_candidate,
                                    &benchmark_names, pairs,
                                    benchmark_warmups);
        } else {
            ok = run_benchmarks(custom_benchmark ? NULL : &config,
                                custom_benchmark ? benchmark_toy : config.toy_exe,
                                &benchmark_names, benchmark_runs,
                                &compile_commands);
        }
    }
    if (ok && needs_core) ok = write_compile_commands(&compile_commands);

    for (size_t i = 0; i < compile_commands.count; ++i) {
        da_free(compile_commands.items[i].command);
    }
    da_free(compile_commands);
    da_free(config.include_dirs);
    da_free(config.library_dirs);
    da_free(config.libraries);
    da_free(benchmark_names);
    return ok ? 0 : 1;
}

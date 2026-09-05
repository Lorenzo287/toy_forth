#ifndef TOY_NOB_BUILD_H
#define TOY_NOB_BUILD_H

/* Compiler configuration, incremental build graph, and SDK staging. */

typedef enum {
    COMPILER_GCC,
    COMPILER_CLANG,
    COMPILER_MSVC,
    COMPILER_CLANG_CL,
} Compiler;

typedef enum {
    MODE_RELEASE,
    MODE_DEBUG,
    MODE_ALLOC,
    MODE_LEAK,
    MODE_OBSERVE,
    MODE_PROFILE,
} Build_Mode;

typedef struct {
    Compiler compiler;
    Build_Mode mode;
    size_t jobs;
    const char *test_filter;
    const char *build_dir;
    const char *object_dir;
    const char *extension_artifact_dir;
    const char *test_package_dir;
    const char *core_package_dir;
    const char *runtime_lib;
    const char *toy_exe;
    const char *dist_dir;
    File_Paths include_dirs;
    File_Paths library_dirs;
    File_Paths libraries;
} Build_Config;

typedef struct {
    const char *file;
    const char *output;
    Cmd command;
} Compile_Command;

typedef struct {
    Compile_Command *items;
    size_t count;
    size_t capacity;
} Compile_Commands;

static const char *runtime_sources[] = {
    "src/tf_alloc.c",
    "src/tf_terminal.c",
    "src/tf_context.c",
    "src/tf_debug_control.c",
    "src/tf_debug_inspect.c",
    "src/tf_dictionary.c",
    "src/generated/tf_docs.c",
    "src/tf_exec.c",
    "src/tf_parser.c",
    "src/tf_builtins_control.c",
    "src/tf_builtins_core.c",
    "src/tf_builtins_data.c",
    "src/tf_builtins_io.c",
    "src/tf_builtins_meta.c",
    "src/tf_builtins_sys.c",
    "src/tf_packages.c",
    "src/tf_package_loader.c",
    "src/tf_native_loader.c",
    "src/tf_obj.c",
    "src/tf_random.c",
    "src/toy.c",
};

static const char *cli_sources[] = {
    "src/cli/main.c",
    "src/cli/tf_debug_protocol.c",
    "src/cli/tf_repl.c",
#ifdef _WIN32
    "deps/linenoise/linenoise_win.c",
#else
    "deps/linenoise/linenoise.c",
#endif
};

static const char *core_c_tests[] = {
    "tests/c/test_embed_api.c",
    "tests/c/test_debug_control.c",
    "tests/c/test_debug_inspection.c",
    "tests/c/test_linenoise.c",
    "tests/c/test_runtime_state.c",
    "tests/c/test_capture_storage.c",
};


#ifndef NOBDEF
#define NOBDEF
#endif

NOBDEF const char *static_library_path(const Build_Config *config, const char *name);
NOBDEF const char *compiler_name(Compiler compiler);
NOBDEF const char *compiler_executable(Compiler compiler);
NOBDEF const char *mode_name(Build_Mode mode);
NOBDEF bool is_msvc_style(Compiler compiler);
NOBDEF bool starts_with(const char *value, const char *prefix);
NOBDEF bool ends_with(const char *value, const char *suffix);
NOBDEF void print_usage(const char *program);
NOBDEF bool parse_compiler(const char *value, Compiler *compiler);
NOBDEF bool parse_mode(const char *value, Build_Mode *mode);
NOBDEF bool parse_count(const char *value, size_t *count);
NOBDEF bool program_on_path(const char *program);
NOBDEF bool ensure_directory(const char *path);
NOBDEF bool configure_paths(Build_Config *config);
NOBDEF char *object_path(const Build_Config *config, const char *source);
NOBDEF bool collect_matching_files(const char *directory, const char *suffix, File_Paths *paths);
NOBDEF bool collect_header_dependencies(const Build_Config *config, File_Paths *dependencies);
NOBDEF void append_mode_compile_flags(Cmd *command, const Build_Config *config);
NOBDEF void append_compile_flags(Cmd *command, const Build_Config *config);
NOBDEF bool library_is_path(const char *library);
NOBDEF void append_link_flags(Cmd *command, const Build_Config *config, bool with_external_libraries);
NOBDEF void record_compile_command(Compile_Commands *commands, const char *source, const char *output, const Cmd *command);
NOBDEF bool write_json_string(FILE *file, const char *value);
NOBDEF bool write_compile_commands(const Compile_Commands *commands);
NOBDEF bool source_needs_rebuild(const char *output, const char *source, const File_Paths *headers, bool *needs_rebuild_out);
NOBDEF bool schedule_compile_options( const Build_Config *config, const char *source, const char *output, const File_Paths *headers, Compile_Commands *compile_commands, Procs *processes, bool shared_object, const File_Paths *additional_include_dirs, const File_Paths *definitions);
NOBDEF bool schedule_compile_ex(const Build_Config *config, const char *source, const char *output, const File_Paths *headers, Compile_Commands *compile_commands, Procs *processes, bool shared_object);
NOBDEF bool schedule_compile(const Build_Config *config, const char *source, const char *output, const File_Paths *headers, Compile_Commands *compile_commands, Procs *processes);
NOBDEF bool archive_library(const Build_Config *config, const char *output, const File_Paths *objects);
NOBDEF bool link_shared_package(const Build_Config *config, const char *output, const File_Paths *objects, bool with_external_libraries);
NOBDEF bool build_extension_source(const Build_Config *config, const char *source, const char *output, Compile_Commands *compile_commands);
NOBDEF bool link_executable_options(const Build_Config *config, const char *output, const File_Paths *objects, bool with_runtime, bool with_external_libraries);
NOBDEF bool link_executable(const Build_Config *config, const char *output, const File_Paths *objects, bool with_runtime);
NOBDEF bool run_generator(const Build_Config *config, const char *manifest, const char *output, const char *package_name);
NOBDEF bool write_package_manifest(const char *directory, const char *name, const char *extension_file);
NOBDEF bool build_core_ffi(const Build_Config *config, Compile_Commands *compile_commands);
NOBDEF bool build_core_random(const Build_Config *config,
                              Compile_Commands *compile_commands);
NOBDEF bool stage_core_source_package(const Build_Config *config,
                                      const char *name);
NOBDEF bool build_core(const Build_Config *config, Compile_Commands *compile_commands);
NOBDEF bool remove_tree(const char *path);
NOBDEF bool copy_sdk_file(const char *source, const char *destination);
NOBDEF bool sdk_tree_directory_allowed(const char *name);
NOBDEF bool sdk_tree_file_allowed(const char *name);
NOBDEF bool copy_sdk_tree(const char *source, const char *destination);
NOBDEF bool build_sdk_tool(const char *root, const Build_Config *config, const char *name);
NOBDEF bool check_distribution_prerequisites(void);
NOBDEF bool build_distribution(const Build_Config *config, const char *root);

#endif // TOY_NOB_BUILD_H

#ifdef NOB_IMPLEMENTATION

NOBDEF const char *static_library_path(const Build_Config *config,
                                       const char *name) {
#if defined(_WIN32)
    if (config->compiler != COMPILER_GCC) {
        return temp_sprintf("%s/%s.lib", config->build_dir, name);
    }
#endif
    return temp_sprintf("%s/lib%s.a", config->build_dir, name);
}

NOBDEF const char *compiler_name(Compiler compiler) {
    switch (compiler) {
    case COMPILER_GCC: return "gcc";
    case COMPILER_CLANG: return "clang";
    case COMPILER_MSVC: return "msvc";
    case COMPILER_CLANG_CL: return "clang-cl";
    }
    return "unknown";
}

NOBDEF const char *compiler_executable(Compiler compiler) {
    switch (compiler) {
    case COMPILER_GCC: return "gcc";
    case COMPILER_CLANG: return "clang";
    case COMPILER_MSVC: return "cl";
    case COMPILER_CLANG_CL: return "clang-cl";
    }
    return "cc";
}

NOBDEF const char *mode_name(Build_Mode mode) {
    switch (mode) {
    case MODE_RELEASE: return "release";
    case MODE_DEBUG: return "debug";
    case MODE_ALLOC: return "alloc";
    case MODE_LEAK: return "leak";
    case MODE_OBSERVE: return "observe";
    case MODE_PROFILE: return "profile";
    }
    return "unknown";
}

NOBDEF bool is_msvc_style(Compiler compiler) {
    return compiler == COMPILER_MSVC || compiler == COMPILER_CLANG_CL;
}

NOBDEF bool starts_with(const char *value, const char *prefix) {
    return strncmp(value, prefix, strlen(prefix)) == 0;
}

NOBDEF bool ends_with(const char *value, const char *suffix) {
    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    return value_len >= suffix_len &&
           strcmp(value + value_len - suffix_len, suffix) == 0;
}

NOBDEF void print_usage(const char *program) {
    fprintf(stderr, "Toy build\n\n");
    fprintf(stderr, "Usage: %s [options] <command>\n\n",
            program);
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  build                 Build the runtime and Toy CLI\n");
    fprintf(stderr, "  test                  Run the default test suite\n");
    fprintf(stderr, "  benchmark [name ...]  Build and run performance workloads\n");
    fprintf(stderr, "  dist                  Stage a complete SDK at dist/toy\n");
    fprintf(stderr, "  clean                 Remove build and dist outputs\n");
    fprintf(stderr, "  help                  Show this help\n\n");
    fprintf(stderr, "Build options:\n");
    fprintf(stderr, "  --cc <compiler>       clang, gcc, msvc, or clang-cl\n");
    fprintf(stderr,
            "  --mode <mode>         release (default), debug, alloc, leak, "
            "observe, profile\n");
    fprintf(stderr, "  -j, --jobs <count>    Maximum parallel compiler processes\n");
    fprintf(stderr, "  --filter <text>       Run matching tests only\n\n");
    fprintf(stderr, "Benchmark options:\n");
    fprintf(stderr, "  --runs <count>        Samples per workload (default: 5)\n");
    fprintf(stderr, "  --toy <path>          Use an existing Toy executable\n");
    fprintf(stderr,
            "  --compare <base> <new> Compare two Toy executables (15 pairs)\n");
    fprintf(stderr,
            "  --warmup <count>       Warmups per executable (default: 1)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Core libffi options (repeatable):\n");
    fprintf(stderr, "  --include <directory> Add a C include directory\n");
    fprintf(stderr, "  --lib-dir <directory> Add a library search directory\n");
    fprintf(stderr, "  --lib <name-or-path>  Link a library\n\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  nob --mode debug build\n");
    fprintf(stderr, "  nob test --filter package\n");
    fprintf(stderr, "  nob benchmark vector --runs 10\n");
    fprintf(stderr,
            "  nob benchmark dispatch --compare old/toy new/toy --runs 15\n");
    fprintf(stderr, "  nob dist\n");
}

NOBDEF bool parse_compiler(const char *value, Compiler *compiler) {
    if (strcmp(value, "gcc") == 0) *compiler = COMPILER_GCC;
    else if (strcmp(value, "clang") == 0) *compiler = COMPILER_CLANG;
    else if (strcmp(value, "msvc") == 0 || strcmp(value, "cl") == 0) {
        *compiler = COMPILER_MSVC;
    } else if (strcmp(value, "clang-cl") == 0) {
        *compiler = COMPILER_CLANG_CL;
    } else {
        return false;
    }
    return true;
}

NOBDEF bool parse_mode(const char *value, Build_Mode *mode) {
    if (strcmp(value, "release") == 0) *mode = MODE_RELEASE;
    else if (strcmp(value, "debug") == 0) *mode = MODE_DEBUG;
    else if (strcmp(value, "alloc") == 0) *mode = MODE_ALLOC;
    else if (strcmp(value, "leak") == 0) *mode = MODE_LEAK;
    else if (strcmp(value, "observe") == 0) *mode = MODE_OBSERVE;
    else if (strcmp(value, "profile") == 0) *mode = MODE_PROFILE;
    else return false;
    return true;
}

NOBDEF bool parse_count(const char *value, size_t *count) {
    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (value[0] == '\0' || *end != '\0' || parsed == 0) return false;
    *count = (size_t)parsed;
    return true;
}

NOBDEF bool program_on_path(const char *program) {
#ifdef _WIN32
    char buffer[MAX_PATH];
    return SearchPathA(NULL, program, ".exe", MAX_PATH, buffer, NULL) != 0;
#else
    const char *path = getenv("PATH");
    if (!path) return false;
    const char *cursor = path;
    while (*cursor) {
        const char *separator = strchr(cursor, ':');
        size_t length = separator ? (size_t)(separator - cursor) : strlen(cursor);
        const char *candidate = temp_sprintf("%.*s/%s", (int)length, cursor,
                                             program);
        if (access(candidate, X_OK) == 0) return true;
        if (!separator) break;
        cursor = separator + 1;
    }
    return false;
#endif
}

NOBDEF bool ensure_directory(const char *path) {
    if (file_exists(path)) return true;
    return mkdir_if_not_exists(path);
}

NOBDEF bool configure_paths(Build_Config *config) {
    config->build_dir = temp_sprintf("build/%s/%s",
                                     compiler_name(config->compiler),
                                     mode_name(config->mode));
    config->object_dir = temp_sprintf("%s/obj", config->build_dir);
    config->extension_artifact_dir =
        temp_sprintf("%s/extensions", config->build_dir);
    config->test_package_dir = temp_sprintf("%s/test-packages",
                                           config->build_dir);
    config->core_package_dir = temp_sprintf("%s/core", config->build_dir);
    config->runtime_lib = static_library_path(config, "toy_runtime");
    config->toy_exe = temp_sprintf("%s/toy%s", config->build_dir,
                                   TOY_EXE_SUFFIX);
    config->dist_dir = "dist/toy";

    if (!ensure_directory("build")) return false;
    if (!ensure_directory(temp_sprintf("build/%s",
                                       compiler_name(config->compiler)))) {
        return false;
    }
    if (!ensure_directory(config->build_dir)) return false;
    if (!ensure_directory(config->object_dir)) return false;
    if (!ensure_directory(config->extension_artifact_dir)) return false;
    if (!ensure_directory(config->test_package_dir)) return false;
    if (!ensure_directory(config->core_package_dir)) return false;
    if (!ensure_directory(temp_sprintf("%s/tests", config->build_dir))) {
        return false;
    }
    if (!ensure_directory(temp_sprintf("%s/generated", config->build_dir))) {
        return false;
    }
    return ensure_directory(temp_sprintf("%s/test-work", config->build_dir));
}

NOBDEF char *object_path(const Build_Config *config, const char *source) {
    char *name = temp_sprintf("%s", source);
    for (char *cursor = name; *cursor; ++cursor) {
        unsigned char character = (unsigned char)*cursor;
        if (!isalnum(character) && *cursor != '_' && *cursor != '-') {
            *cursor = '_';
        }
    }
#ifdef _WIN32
    return temp_sprintf("%s/%s.obj", config->object_dir, name);
#else
    return temp_sprintf("%s/%s.o", config->object_dir, name);
#endif
}

NOBDEF bool collect_matching_files(const char *directory, const char *suffix,
                                   File_Paths *paths) {
    File_Paths entries = {0};
    if (!read_entire_dir(directory, &entries)) return false;
    for (size_t i = 0; i < entries.count; ++i) {
        const char *name = entries.items[i];
        if (ends_with(name, suffix)) {
            da_append(paths, temp_sprintf("%s/%s", directory, name));
        }
    }
    da_free(entries);
    return true;
}

NOBDEF bool collect_header_dependencies(const Build_Config *config,
                                        File_Paths *dependencies) {
    da_append(dependencies, "nob.c");
    da_append(dependencies, "tools/nob/build.h");
    da_append(dependencies, "deps/nob/nob.h");
    if (!collect_matching_files("include", ".h", dependencies)) return false;
    if (!collect_matching_files("src", ".h", dependencies)) return false;
    if (!collect_matching_files("src/cli", ".h", dependencies)) return false;
    if (!collect_matching_files("src/generated", ".inc", dependencies)) return false;
    da_append(dependencies, "deps/linenoise/linenoise.h");
    if (config->mode == MODE_LEAK && file_exists("deps/stb_leakcheck/stb_leakcheck.h")) {
        da_append(dependencies, "deps/stb_leakcheck/stb_leakcheck.h");
    }
    return true;
}

NOBDEF void append_mode_compile_flags(Cmd *command,
                                      const Build_Config *config) {
    if (is_msvc_style(config->compiler)) {
        switch (config->mode) {
        case MODE_RELEASE: cmd_append(command, "/O2", "/DNDEBUG"); break;
        case MODE_DEBUG: cmd_append(command, "/Od", "/Z7"); break;
        case MODE_ALLOC:
            cmd_append(command, "/O2", "/DNDEBUG", "/DTF_ALLOC_STATS");
            break;
        case MODE_LEAK:
            cmd_append(command, "/Od", "/Z7", "/DSTB_LEAKCHECK", "/Ideps");
            break;
        case MODE_OBSERVE:
            cmd_append(command, "/O2", "/DNDEBUG", "/DTF_OBSERVE");
            break;
        case MODE_PROFILE:
            cmd_append(command, "/O2", "/Z7", "/Oy-");
            break;
        }
    } else {
        switch (config->mode) {
        case MODE_RELEASE: cmd_append(command, "-O3", "-DNDEBUG"); break;
        case MODE_DEBUG: cmd_append(command, "-O0", "-g"); break;
        case MODE_ALLOC:
            cmd_append(command, "-O3", "-DNDEBUG", "-DTF_ALLOC_STATS");
            break;
        case MODE_LEAK:
#ifdef _WIN32
            cmd_append(command, "-O0", "-g", "-DSTB_LEAKCHECK", "-Ideps");
#else
            cmd_append(command, "-O0", "-g", "-fsanitize=leak",
                       "-fno-omit-frame-pointer");
#endif
            break;
        case MODE_OBSERVE:
            cmd_append(command, "-O3", "-DNDEBUG", "-DTF_OBSERVE");
            break;
        case MODE_PROFILE:
            cmd_append(command, "-O2", "-g", "-fno-omit-frame-pointer");
            break;
        }
    }
}

NOBDEF void append_compile_flags(Cmd *command, const Build_Config *config) {
    cmd_append(command, compiler_executable(config->compiler));
    if (is_msvc_style(config->compiler)) {
        cmd_append(command, "/nologo", "/std:c11", "/W3",
                   "/D_CRT_SECURE_NO_WARNINGS", "/Iinclude", "/Isrc",
                   "/Ideps/linenoise",
                   "/DTOY_SHARED_SUFFIX=\"" TOY_SHARED_SUFFIX_VALUE "\"");
    } else {
        cmd_append(command, "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
                   "-Iinclude", "-Isrc", "-Ideps/linenoise",
                   "-DTOY_SHARED_SUFFIX=\"" TOY_SHARED_SUFFIX_VALUE "\"");
#if !defined(_WIN32)
        /*
         * The Unix linenoise implementation uses POSIX functions such as
         * strdup, fchmod, and fileno.  With strict C11, recent glibc headers
         * only declare these when the POSIX feature level is selected.
         */
        cmd_append(command, "-D_POSIX_C_SOURCE=200809L");
#endif
#if defined(_WIN32)
        if (config->compiler == COMPILER_CLANG) {
            cmd_append(command, "-D_CRT_SECURE_NO_WARNINGS");
        }
#endif
    }
    for (size_t i = 0; i < config->include_dirs.count; ++i) {
        if (is_msvc_style(config->compiler)) {
            cmd_append(command, temp_sprintf("/I%s",
                                             config->include_dirs.items[i]));
        } else {
            cmd_append(command, "-I", config->include_dirs.items[i]);
        }
    }
    append_mode_compile_flags(command, config);
}

NOBDEF bool library_is_path(const char *library) {
    return strchr(library, '/') || strchr(library, '\\') ||
           ends_with(library, ".a") || ends_with(library, ".lib") ||
           ends_with(library, ".so") || ends_with(library, ".dylib") ||
           ends_with(library, ".dll");
}

NOBDEF void append_link_flags(Cmd *command, const Build_Config *config,
                              bool with_external_libraries) {
    if (is_msvc_style(config->compiler)) {
        if (with_external_libraries) {
            for (size_t i = 0; i < config->libraries.count; ++i) {
                const char *library = config->libraries.items[i];
                cmd_append(command, library_is_path(library) ? library :
                           temp_sprintf("%s.lib", library));
            }
        }
#ifdef _WIN32
        cmd_append(command, "user32.lib");
#endif
        bool needs_link_options =
            (with_external_libraries && config->library_dirs.count > 0) ||
            config->mode == MODE_PROFILE || config->mode == MODE_DEBUG ||
            config->mode == MODE_LEAK;
        if (needs_link_options) cmd_append(command, "/link");
        if (with_external_libraries) {
            for (size_t i = 0; i < config->library_dirs.count; ++i) {
                cmd_append(command, temp_sprintf("/LIBPATH:%s",
                           config->library_dirs.items[i]));
            }
        }
        if (config->mode == MODE_PROFILE || config->mode == MODE_DEBUG ||
            config->mode == MODE_LEAK) {
            cmd_append(command, "/DEBUG");
        }
    } else {
        if (with_external_libraries) {
            for (size_t i = 0; i < config->library_dirs.count; ++i) {
                cmd_append(command, "-L", config->library_dirs.items[i]);
            }
            for (size_t i = 0; i < config->libraries.count; ++i) {
                const char *library = config->libraries.items[i];
                cmd_append(command, library_is_path(library) ? library :
                           temp_sprintf("-l%s", library));
            }
        }
#ifdef _WIN32
        cmd_append(command, "-luser32");
        if (config->compiler == COMPILER_CLANG &&
            (config->mode == MODE_PROFILE || config->mode == MODE_DEBUG ||
             config->mode == MODE_LEAK)) {
            cmd_append(command, "-Xlinker", "/DEBUG");
        }
#else
        cmd_append(command, "-lm");
#ifndef __APPLE__
        cmd_append(command, "-ldl");
#endif
        if (config->mode == MODE_LEAK) cmd_append(command, "-fsanitize=leak");
#endif
    }
}

NOBDEF void record_compile_command(Compile_Commands *commands,
                                   const char *source, const char *output,
                                   const Cmd *command) {
    Compile_Command record = {
        .file = source,
        .output = output,
        .command = {0},
    };
    da_append_many(&record.command, command->items, command->count);
    da_append(commands, record);
}

NOBDEF bool write_json_string(FILE *file, const char *value) {
    if (fputc('"', file) == EOF) return false;
    for (const unsigned char *cursor = (const unsigned char *)value;
         *cursor; ++cursor) {
        switch (*cursor) {
        case '"': if (fputs("\\\"", file) < 0) return false; break;
        case '\\': if (fputs("\\\\", file) < 0) return false; break;
        case '\b': if (fputs("\\b", file) < 0) return false; break;
        case '\f': if (fputs("\\f", file) < 0) return false; break;
        case '\n': if (fputs("\\n", file) < 0) return false; break;
        case '\r': if (fputs("\\r", file) < 0) return false; break;
        case '\t': if (fputs("\\t", file) < 0) return false; break;
        default:
            if (*cursor < 0x20) {
                if (fprintf(file, "\\u%04x", *cursor) < 0) return false;
            } else if (fputc(*cursor, file) == EOF) {
                return false;
            }
        }
    }
    return fputc('"', file) != EOF;
}

NOBDEF bool write_compile_commands(const Compile_Commands *commands) {
    const char *directory = get_current_dir_temp();
    FILE *file = fopen("build/compile_commands.json", "wb");
    if (!file) {
        nob_log(ERROR, "could not write compile_commands.json: %s",
                strerror(errno));
        return false;
    }

    bool ok = fputs("[\n", file) >= 0;
    for (size_t i = 0; ok && i < commands->count; ++i) {
        const Compile_Command *record = &commands->items[i];
        ok = fputs("  {\"directory\":", file) >= 0;
        if (ok) ok = write_json_string(file, directory);
        if (ok) ok = fputs(",\"file\":", file) >= 0;
        if (ok) ok = write_json_string(file, record->file);
        if (ok) ok = fputs(",\"output\":", file) >= 0;
        if (ok) ok = write_json_string(file, record->output);
        if (ok) ok = fputs(",\"arguments\":[", file) >= 0;
        for (size_t j = 0; ok && j < record->command.count; ++j) {
            if (j > 0) ok = fputc(',', file) != EOF;
            if (ok) ok = write_json_string(file, record->command.items[j]);
        }
        if (ok) ok = fputs(i + 1 == commands->count ? "]}\n" : "]},\n",
                           file) >= 0;
    }
    if (ok) ok = fputs("]\n", file) >= 0;
    if (fclose(file) != 0) ok = false;
    if (ok) nob_log(INFO, "generated compile_commands.json");
    return ok;
}

NOBDEF bool source_needs_rebuild(const char *output, const char *source,
                                 const File_Paths *headers,
                                 bool *needs_rebuild_out) {
    File_Paths inputs = {0};
    da_append(&inputs, source);
    da_append_many(&inputs, headers->items, headers->count);
    int result = needs_rebuild(output, inputs.items, inputs.count);
    da_free(inputs);
    if (result < 0) return false;
    *needs_rebuild_out = result != 0;
    return true;
}

NOBDEF bool schedule_compile_options(
    const Build_Config *config, const char *source, const char *output,
    const File_Paths *headers, Compile_Commands *compile_commands,
    Procs *processes, bool shared_object,
    const File_Paths *additional_include_dirs,
    const File_Paths *definitions) {
    Cmd command = {0};
    append_compile_flags(&command, config);
    if (is_msvc_style(config->compiler) &&
        starts_with(source, "deps/linenoise/")) {
        cmd_append(&command, "/wd4267");
    }
    if (shared_object && !is_msvc_style(config->compiler)) {
#ifndef _WIN32
        cmd_append(&command, "-fPIC");
#endif
        cmd_append(&command, "-fvisibility=hidden");
    }
    if (additional_include_dirs) {
        for (size_t i = 0; i < additional_include_dirs->count; ++i) {
            if (is_msvc_style(config->compiler)) {
                cmd_append(&command, temp_sprintf("/I%s",
                           additional_include_dirs->items[i]));
            } else {
                cmd_append(&command, "-I", additional_include_dirs->items[i]);
            }
        }
    }
    if (definitions) {
        for (size_t i = 0; i < definitions->count; ++i) {
            cmd_append(&command, temp_sprintf(
                is_msvc_style(config->compiler) ? "/D%s" : "-D%s",
                definitions->items[i]));
        }
    }
    if (is_msvc_style(config->compiler)) {
        cmd_append(&command, "/c", source, temp_sprintf("/Fo:%s", output));
    } else {
        cmd_append(&command, "-c", source, "-o", output);
    }
    record_compile_command(compile_commands, source, output, &command);

    bool rebuild = false;
    if (!source_needs_rebuild(output, source, headers, &rebuild)) return false;
    if (!rebuild) return true;

    if (!cmd_run(&command, .async = processes, .max_procs = config->jobs)) {
        return false;
    }
    return true;
}

NOBDEF bool schedule_compile_ex(const Build_Config *config,
                                const char *source, const char *output,
                                const File_Paths *headers,
                                Compile_Commands *compile_commands,
                                Procs *processes, bool shared_object) {
    return schedule_compile_options(config, source, output, headers,
                                    compile_commands, processes,
                                    shared_object, NULL, NULL);
}

NOBDEF bool schedule_compile(const Build_Config *config, const char *source,
                             const char *output, const File_Paths *headers,
                             Compile_Commands *compile_commands,
                             Procs *processes) {
    return schedule_compile_ex(config, source, output, headers,
                               compile_commands, processes, false);
}

NOBDEF bool archive_library(const Build_Config *config, const char *output,
                            const File_Paths *objects) {
    int rebuild = needs_rebuild(output, objects->items, objects->count);
    if (rebuild < 0) return false;
    if (!rebuild) return true;
    if (file_exists(output) && !delete_file(output)) {
        return false;
    }

    Cmd command = {0};
#if defined(_WIN32)
    if (config->compiler == COMPILER_CLANG ||
        config->compiler == COMPILER_CLANG_CL) {
        cmd_append(&command, "llvm-lib", "/nologo",
                   temp_sprintf("/OUT:%s", output));
    } else if (config->compiler == COMPILER_MSVC) {
        cmd_append(&command, "lib", "/nologo",
                   temp_sprintf("/OUT:%s", output));
    } else
#endif
    {
        cmd_append(&command, "ar", "rcs", output);
    }
    da_append_many(&command, objects->items, objects->count);
    return cmd_run(&command, .dont_reset = false);
}

NOBDEF bool link_shared_package(const Build_Config *config,
                               const char *output,
                               const File_Paths *objects,
                               bool with_external_libraries) {
    int rebuild = needs_rebuild(output, objects->items, objects->count);
    if (rebuild < 0) return false;
    if (!rebuild) return true;

    Cmd command = {0};
    cmd_append(&command, compiler_executable(config->compiler));
    if (is_msvc_style(config->compiler)) {
        cmd_append(&command, "/nologo", "/LD");
        da_append_many(&command, objects->items, objects->count);
        cmd_append(&command, temp_sprintf("/Fe:%s", output));
    } else {
#ifdef __APPLE__
        cmd_append(&command, "-dynamiclib");
#else
        cmd_append(&command, "-shared");
#endif
        da_append_many(&command, objects->items, objects->count);
        cmd_append(&command, "-o", output);
    }
    append_link_flags(&command, config, with_external_libraries);
    return cmd_run(&command, .dont_reset = false);
}


NOBDEF bool build_extension_source(const Build_Config *config,
                                   const char *source, const char *output,
                                   Compile_Commands *compile_commands) {
    if (!file_exists(source)) {
        nob_log(ERROR, "C extension source does not exist: %s", source);
        return false;
    }

    File_Paths headers = {0};
    File_Paths objects = {0};
    Procs processes = {0};
    const char *object = object_path(config, source);
    da_append(&objects, object);

    bool ok = collect_header_dependencies(config, &headers);
    if (ok) {
        ok = schedule_compile_ex(config, source, object, &headers,
                                 compile_commands, &processes, true);
    }
    if (!procs_flush(&processes)) ok = false;
    if (ok) ok = link_shared_package(config, output, &objects, true);

    da_free(processes);
    da_free(headers);
    da_free(objects);
    return ok;
}

NOBDEF bool link_executable_options(const Build_Config *config,
                                    const char *output,
                                    const File_Paths *objects,
                                    bool with_runtime,
                                    bool with_external_libraries) {
    File_Paths inputs = {0};
    da_append_many(&inputs, objects->items, objects->count);
    if (with_runtime) da_append(&inputs, config->runtime_lib);
    int rebuild = needs_rebuild(output, inputs.items, inputs.count);
    da_free(inputs);
    if (rebuild < 0) return false;
    if (!rebuild) return true;

    Cmd command = {0};
    cmd_append(&command, compiler_executable(config->compiler));
    if (is_msvc_style(config->compiler)) cmd_append(&command, "/nologo");
    da_append_many(&command, objects->items, objects->count);
    if (with_runtime) cmd_append(&command, config->runtime_lib);
    if (is_msvc_style(config->compiler)) {
        cmd_append(&command, temp_sprintf("/Fe:%s", output));
    } else {
        cmd_append(&command, "-o", output);
    }
    append_link_flags(&command, config, with_external_libraries);
    return cmd_run(&command, .dont_reset = false);
}

NOBDEF bool link_executable(const Build_Config *config, const char *output,
                            const File_Paths *objects, bool with_runtime) {
    return link_executable_options(config, output, objects, with_runtime,
                                   false);
}

NOBDEF bool run_generator(const Build_Config *config, const char *manifest,
                          const char *output, const char *package_name) {
    (void)config;
    if (!program_on_path("node")) {
        nob_log(ERROR, "Node.js is required to generate C bindings");
        return false;
    }
    Cmd command = {0};
    cmd_append(&command, "node", "tools/generate-binding.js", "--package",
               package_name, manifest, output);
    return cmd_run(&command, .dont_reset = false);
}

NOBDEF bool write_package_manifest(const char *directory, const char *name,
                                   const char *extension_file) {
    const char *path = temp_sprintf("%s/toy.package", directory);
    FILE *file = fopen(path, "wb");
    if (!file) {
        nob_log(ERROR, "could not write %s: %s", path, strerror(errno));
        return false;
    }
    bool ok = fprintf(file, "name = %s\nextension = %s\n", name,
                      extension_file) >= 0;
    if (fclose(file) != 0) ok = false;
    if (!ok) nob_log(ERROR, "could not write %s", path);
    return ok;
}

NOBDEF bool build_core_ffi(const Build_Config *config,
                           Compile_Commands *compile_commands) {
    const char *directory = temp_sprintf("%s/ffi", config->core_package_dir);
    if (!ensure_directory(directory)) return false;
    const char *extension_file = temp_sprintf("toy_ffi%s",
                                              TOY_SHARED_SUFFIX_VALUE);

    Build_Config ffi_config = *config;
    ffi_config.libraries = (File_Paths){0};
    da_append_many(&ffi_config.libraries, config->libraries.items,
                   config->libraries.count);
    if (ffi_config.libraries.count == 0) {
        da_append(&ffi_config.libraries, "ffi");
    }
    const char *output = temp_sprintf("%s/%s", directory, extension_file);
    bool ok = build_extension_source(&ffi_config, "core/ffi/toy_ffi.c",
                                     output, compile_commands);
    if (ok) ok = write_package_manifest(directory, "ffi", extension_file);
    da_free(ffi_config.libraries);
    if (ok) nob_log(INFO, "built core package %s", output);
    return ok;
}

NOBDEF bool build_core_random(const Build_Config *config,
                              Compile_Commands *compile_commands) {
    const char *directory =
        temp_sprintf("%s/random", config->core_package_dir);
    if (!remove_tree(directory) || !ensure_directory(directory)) return false;

    bool ok = copy_sdk_file("core/random/random.toy",
                            temp_sprintf("%s/random.toy", directory)) &&
              copy_sdk_file("core/random/words.json",
                            temp_sprintf("%s/words.json", directory));
    const char *extension_file = temp_sprintf("toy_random%s",
                                              TOY_SHARED_SUFFIX_VALUE);
    const char *output = temp_sprintf("%s/%s", directory, extension_file);
    Build_Config random_config = *config;
    random_config.libraries = (File_Paths){0};
    if (ok) {
        ok = build_extension_source(&random_config,
                                    "core/random/toy_random.c", output,
                                    compile_commands);
    }
    if (ok) ok = write_package_manifest(directory, "random", extension_file);
    if (ok) nob_log(INFO, "built core package %s", output);
    return ok;
}

NOBDEF bool stage_core_source_package(const Build_Config *config,
                                      const char *name) {
    const char *source = temp_sprintf("core/%s", name);
    const char *destination = temp_sprintf("%s/%s", config->core_package_dir,
                                           name);
    if (!remove_tree(destination) || !copy_sdk_tree(source, destination)) {
        return false;
    }
    nob_log(INFO, "staged core package %s", destination);
    return true;
}

NOBDEF bool build_core(const Build_Config *config,
                       Compile_Commands *compile_commands) {
    File_Paths headers = {0};
    File_Paths runtime_objects = {0};
    File_Paths cli_objects = {0};
    Procs processes = {0};
    bool ok = collect_header_dependencies(config, &headers);

    for (size_t i = 0; ok && i < ARRAY_LEN(runtime_sources); ++i) {
        const char *output = object_path(config, runtime_sources[i]);
        da_append(&runtime_objects, output);
        ok = schedule_compile(config, runtime_sources[i], output, &headers,
                              compile_commands, &processes);
    }
    for (size_t i = 0; ok && i < ARRAY_LEN(cli_sources); ++i) {
        const char *output = object_path(config, cli_sources[i]);
        da_append(&cli_objects, output);
        ok = schedule_compile(config, cli_sources[i], output, &headers,
                              compile_commands, &processes);
    }
    if (!procs_flush(&processes)) ok = false;
    if (ok) {
        ok = archive_library(config, config->runtime_lib, &runtime_objects);
    }
    if (ok) ok = link_executable(config, config->toy_exe, &cli_objects, true);
    if (ok) ok = build_core_ffi(config, compile_commands);
    if (ok) ok = stage_core_source_package(config, "json");
    if (ok) ok = build_core_random(config, compile_commands);

    da_free(processes);
    da_free(headers);
    da_free(runtime_objects);
    da_free(cli_objects);
    if (ok) {
        nob_log(INFO, "built %s", config->toy_exe);
        nob_log(INFO, "built %s", config->runtime_lib);
    }
    return ok;
}

NOBDEF bool remove_tree(const char *path) {
    if (!file_exists(path)) return true;
    Nob_File_Type type = get_file_type(path);
    if (type != FILE_DIRECTORY) return delete_file(path);

    File_Paths children = {0};
    if (!read_entire_dir(path, &children)) return false;
    bool ok = true;
    for (size_t i = 0; ok && i < children.count; ++i) {
        const char *name = children.items[i];
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        ok = remove_tree(temp_sprintf("%s/%s", path, name));
    }
    da_free(children);
    return ok && delete_file(path);
}

NOBDEF bool copy_sdk_file(const char *source, const char *destination) {
    if (!file_exists(source)) {
        nob_log(ERROR, "distribution input does not exist: %s", source);
        return false;
    }
    return copy_file(source, destination);
}

NOBDEF bool sdk_tree_directory_allowed(const char *name) {
    return name[0] != '.' && strcmp(name, "build") != 0 &&
           strcmp(name, "bin") != 0 && strcmp(name, "obj") != 0 &&
           strcmp(name, "generated") != 0 &&
           strcmp(name, "node_modules") != 0;
}

NOBDEF bool sdk_tree_file_allowed(const char *name) {
    if (strcmp(name, "generated.c") == 0) return false;
    return strcmp(name, ".toyfmt") == 0 || ends_with(name, ".md") ||
           ends_with(name, ".toy") || ends_with(name, ".json") ||
           ends_with(name, ".c") || ends_with(name, ".h") ||
           ends_with(name, ".csv") || ends_with(name, ".sql") ||
           ends_with(name, ".png") || ends_with(name, ".jpg") ||
           ends_with(name, ".jpeg") || ends_with(name, ".gif") ||
           ends_with(name, ".svg") || ends_with(name, ".webp");
}

NOBDEF bool copy_sdk_tree(const char *source, const char *destination) {
    if (!ensure_directory(destination)) return false;

    File_Paths children = {0};
    if (!read_entire_dir(source, &children)) return false;
    bool ok = true;
    for (size_t i = 0; ok && i < children.count; ++i) {
        const char *name = children.items[i];
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        const char *source_path = temp_sprintf("%s/%s", source, name);
        const char *destination_path = temp_sprintf("%s/%s", destination,
                                                    name);
        Nob_File_Type type = get_file_type(source_path);
        if (type == FILE_DIRECTORY) {
            if (sdk_tree_directory_allowed(name)) {
                ok = copy_sdk_tree(source_path, destination_path);
            }
        } else if (sdk_tree_file_allowed(name)) {
            ok = copy_sdk_file(source_path, destination_path);
        }
    }
    da_free(children);
    return ok;
}

NOBDEF bool build_sdk_tool(const char *root, const Build_Config *config,
                           const char *name) {
    const char *output = temp_sprintf("%s/%s/bin/%s%s", root,
                                      config->dist_dir, name,
                                      TOY_EXE_SUFFIX);
    Cmd command = {0};
    cmd_append(&command, "go", "-C", "tools", "build", "-o",
               output, temp_sprintf("./cmd/%s", name));
    return cmd_run(&command, .dont_reset = false);
}

NOBDEF bool check_distribution_prerequisites(void) {
    if (!program_on_path("go")) {
        nob_log(ERROR, "Go is required to build the SDK tooling");
        return false;
    }
    if (!file_exists("tools/tree-sitter-toy/src/parser.c")) {
        nob_log(ERROR,
                "generated Tree-sitter parsers are missing; run npm ci, npm "
                "rebuild tree-sitter-cli, and npm run generate in "
                "tools/tree-sitter-toy");
        return false;
    }
    return true;
}

NOBDEF bool build_distribution(const Build_Config *config, const char *root) {
    static const char *documentation[] = {
        "bindgen.md",
        "c-libraries.md",
        "combinators.md",
        "data-model.md",
        "editor.md",
        "embedding.md",
        "idioms.md",
        "installation.md",
        "core/json.md",
        "packages.md",
        "core/random.md",
        "repl.md",
    };
    const char *dist_parent = "dist";
    Nob_Log_Level previous_level = minimal_log_level;
    minimal_log_level = WARNING;
    bool removed = remove_tree(dist_parent);
    minimal_log_level = previous_level;
    if (!removed || !ensure_directory(dist_parent) ||
        !ensure_directory(config->dist_dir)) {
        return false;
    }
    const char *bin = temp_sprintf("%s/bin", config->dist_dir);
    const char *include = temp_sprintf("%s/include", config->dist_dir);
    const char *lib = temp_sprintf("%s/lib", config->dist_dir);
    const char *docs = temp_sprintf("%s/docs", config->dist_dir);
    const char *core_docs = temp_sprintf("%s/core", docs);
    const char *share = temp_sprintf("%s/share", config->dist_dir);
    const char *toy_share = temp_sprintf("%s/toy", share);
    const char *bindgen_share = temp_sprintf("%s/bindgen", toy_share);
    const char *tree_sitter = temp_sprintf("%s/tree-sitter-toy", toy_share);
    if (!ensure_directory(bin) || !ensure_directory(include) ||
        !ensure_directory(lib) || !ensure_directory(docs) ||
        !ensure_directory(core_docs) ||
        !ensure_directory(share) ||
        !ensure_directory(toy_share) || !ensure_directory(bindgen_share) ||
        !ensure_directory(tree_sitter)) {
        return false;
    }

    bool ok = copy_sdk_file(
        config->toy_exe,
        temp_sprintf("%s/toy%s", bin, TOY_EXE_SUFFIX));
    if (ok) {
        ok = copy_directory_recursively(
            config->core_package_dir,
            temp_sprintf("%s/core", config->dist_dir));
    }
    if (ok) {
        ok = copy_sdk_file("include/toy.h", temp_sprintf("%s/toy.h", include));
    }
    if (ok) {
        ok = copy_sdk_file(config->runtime_lib,
                           temp_sprintf("%s/%s", lib,
                                        path_name(config->runtime_lib)));
    }
    if (ok) {
        ok = copy_sdk_file(
            "tools/generate-binding.js",
            temp_sprintf("%s/generate-binding.js", bindgen_share));
    }
    if (ok) {
        ok = copy_sdk_tree("tools/tree-sitter-toy/src",
                           temp_sprintf("%s/src", tree_sitter)) &&
             copy_directory_recursively(
                 "tools/tree-sitter-toy/queries",
                 temp_sprintf("%s/queries", tree_sitter)) &&
             copy_sdk_file("tools/tree-sitter-toy/grammar.js",
                           temp_sprintf("%s/grammar.js", tree_sitter)) &&
             copy_sdk_file("tools/tree-sitter-toy/tree-sitter.json",
                           temp_sprintf("%s/tree-sitter.json", tree_sitter));
    }
    if (ok) {
        ok = copy_sdk_tree(
            "examples", temp_sprintf("%s/examples", config->dist_dir));
    }
    for (size_t i = 0; ok && i < ARRAY_LEN(documentation); ++i) {
        ok = copy_sdk_file(temp_sprintf("docs/%s", documentation[i]),
                           temp_sprintf("%s/%s", docs, documentation[i]));
    }
    if (ok) {
        ok = copy_sdk_file("README.md",
                           temp_sprintf("%s/README.md", config->dist_dir)) &&
             copy_sdk_file("LICENSE",
                           temp_sprintf("%s/LICENSE", config->dist_dir)) &&
             copy_sdk_file("tools/install.ps1",
                           temp_sprintf("%s/install.ps1", config->dist_dir)) &&
             copy_sdk_file("tools/install.sh",
                           temp_sprintf("%s/install.sh", config->dist_dir));
    }

    const char *tools[] = {
        "toy-lsp", "toy-dap", "toyfmt", "toy-bindgen", "toy-c-package",
    };
    for (size_t i = 0; ok && i < ARRAY_LEN(tools); ++i) {
        ok = build_sdk_tool(root, config, tools[i]);
    }
    if (ok) nob_log(INFO, "staged Toy SDK at %s", config->dist_dir);
    return ok;
}

#endif // NOB_IMPLEMENTATION

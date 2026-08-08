#ifndef TOY_NOB_BENCHMARKS_H
#define TOY_NOB_BENCHMARKS_H

/* Cross-platform benchmark discovery, sampling, and wall-clock reporting. */

#ifndef NOBDEF
#define NOBDEF
#endif

NOBDEF bool collect_benchmarks(const File_Paths *requested, bool include_c,
                               File_Paths *paths);
NOBDEF bool run_benchmarks(const Build_Config *config, const char *toy,
                           const File_Paths *requested, size_t runs,
                           Compile_Commands *compile_commands);

#endif  // TOY_NOB_BENCHMARKS_H

#ifdef NOB_IMPLEMENTATION

static int compare_benchmark_names(const void *left, const void *right) {
    const char *const *a = left;
    const char *const *b = right;
    return strcmp(*a, *b);
}

static int compare_samples(const void *left, const void *right) {
    uint64_t a = *(const uint64_t *)left;
    uint64_t b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static const char *benchmark_path(const char *filename) {
    const char *path = starts_with(filename, "benchmarks/") ||
                               starts_with(filename, "benchmarks\\")
        ? filename
        : temp_sprintf("benchmarks/%s", filename);
    return path;
}

static bool append_benchmark_path(File_Paths *paths, const char *name,
                                  bool include_c) {
    if (ends_with(name, ".toy") || ends_with(name, ".c")) {
        const char *path = benchmark_path(name);
        if (ends_with(name, ".c") && !include_c) {
            nob_log(ERROR,
                    "C benchmarks cannot be run with a custom --toy executable: %s",
                    path);
            return false;
        }
        if (!file_exists(path)) {
            nob_log(ERROR, "benchmark does not exist: %s", path);
            return false;
        }
        da_append(paths, path);
        return true;
    }

    const char *toy_path = benchmark_path(temp_sprintf("%s.toy", name));
    if (file_exists(toy_path)) {
        da_append(paths, toy_path);
        return true;
    }

    const char *c_path = benchmark_path(temp_sprintf("%s.c", name));
    if (file_exists(c_path)) {
        if (!include_c) {
            nob_log(ERROR,
                    "C benchmarks cannot be run with a custom --toy executable: %s",
                    c_path);
            return false;
        }
        da_append(paths, c_path);
        return true;
    }

    nob_log(ERROR, "benchmark does not exist: %s or %s", toy_path, c_path);
    return false;
}

NOBDEF bool collect_benchmarks(const File_Paths *requested, bool include_c,
                               File_Paths *paths) {
    if (requested->count > 0) {
        for (size_t i = 0; i < requested->count; ++i) {
            if (!append_benchmark_path(paths, requested->items[i], include_c)) {
                return false;
            }
        }
        return true;
    }

    File_Paths entries = {0};
    if (!read_entire_dir("benchmarks", &entries)) return false;
    qsort(entries.items, entries.count, sizeof(entries.items[0]),
          compare_benchmark_names);
    for (size_t i = 0; i < entries.count; ++i) {
        bool supported = ends_with(entries.items[i], ".toy") ||
                         (include_c && ends_with(entries.items[i], ".c"));
        if (supported &&
            !append_benchmark_path(paths, entries.items[i], include_c)) {
            da_free(entries);
            return false;
        }
    }
    da_free(entries);
    return true;
}

static bool build_c_benchmark(const Build_Config *config, const char *source,
                              Compile_Commands *compile_commands,
                              const char **executable) {
    const char *directory = temp_sprintf("%s/benchmarks", config->build_dir);
    if (!ensure_directory(directory)) return false;

    const char *filename = path_name(source);
    size_t name_length = strlen(filename) - strlen(".c");
    *executable = temp_sprintf("%s/%.*s%s", directory, (int)name_length,
                               filename, TOY_EXE_SUFFIX);

    File_Paths headers = {0};
    File_Paths objects = {0};
    Procs processes = {0};
    const char *object = object_path(config, source);
    da_append(&objects, object);

    bool ok = collect_header_dependencies(config, &headers);
    if (ok) {
        ok = schedule_compile(config, source, object, &headers,
                              compile_commands, &processes);
    }
    if (!procs_flush(&processes)) ok = false;
    if (ok) ok = link_executable(config, *executable, &objects, true);

    da_free(processes);
    da_free(headers);
    da_free(objects);
    return ok;
}

NOBDEF bool run_benchmarks(const Build_Config *config, const char *toy,
                           const File_Paths *requested, size_t runs,
                           Compile_Commands *compile_commands) {
    if (!file_exists(toy)) {
        nob_log(ERROR, "Toy executable does not exist: %s", toy);
        return false;
    }

    File_Paths paths = {0};
    if (!collect_benchmarks(requested, config != NULL, &paths)) {
        da_free(paths);
        return false;
    }

    uint64_t *samples = malloc(sizeof(*samples) * runs);
    if (!samples) {
        nob_log(ERROR, "could not allocate benchmark samples");
        da_free(paths);
        return false;
    }

    bool ok = true;
    for (size_t i = 0; ok && i < paths.count; ++i) {
        const char *path = paths.items[i];
        const char *c_executable = NULL;
        if (ends_with(path, ".c")) {
            if (!config || !build_c_benchmark(config, path, compile_commands,
                                              &c_executable)) {
                ok = false;
                break;
            }
        }

        fprintf(stderr, "\n%s\n", path_name(path));
        for (size_t run = 0; run < runs; ++run) {
            Cmd command = {0};
            if (c_executable) {
                cmd_append(&command, c_executable);
            } else {
                cmd_append(&command, toy, "--file", path);
            }
            uint64_t start = nanos_since_unspecified_epoch();
            Nob_Log_Level previous_level = minimal_log_level;
            minimal_log_level = WARNING;
            bool ran = cmd_run(&command);
            minimal_log_level = previous_level;
            if (!ran) {
                ok = false;
                break;
            }
            samples[run] = nanos_since_unspecified_epoch() - start;
            fprintf(stderr, "run %zu: %.3f ms wall\n", run + 1,
                    (double)samples[run] / 1000000.0);
        }
        if (!ok) break;

        qsort(samples, runs, sizeof(*samples), compare_samples);
        double median = runs % 2 == 1
            ? (double)samples[runs / 2]
            : ((double)samples[runs / 2 - 1] +
               (double)samples[runs / 2]) / 2.0;
        fprintf(stderr, "median: %.3f ms wall\n", median / 1000000.0);
    }

    free(samples);
    da_free(paths);
    return ok;
}

#endif  // NOB_IMPLEMENTATION

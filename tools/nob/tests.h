#ifndef TOY_NOB_TESTS_H
#define TOY_NOB_TESTS_H

#ifndef _WIN32
#include <signal.h>
#endif

/* Isolated Toy cases plus C API and loadable-package regressions. */

#ifndef NOBDEF
#define NOBDEF
#endif

NOBDEF bool run_all_tests(const Build_Config *config, const char *root, Compile_Commands *compile_commands);

#endif  // TOY_NOB_TESTS_H

#ifdef NOB_IMPLEMENTATION

static bool wait_for_process(Proc process, uint64_t timeout_ms,
                             int *exit_code) {
#ifdef _WIN32
    DWORD result = WaitForSingleObject(process, (DWORD)timeout_ms);
    if (result == WAIT_TIMEOUT) {
        TerminateProcess(process, 124);
        WaitForSingleObject(process, INFINITE);
        CloseHandle(process);
        *exit_code = 124;
        return true;
    }
    if (result == WAIT_FAILED) {
        nob_log(ERROR, "could not wait for test process: %s",
                win32_error_message(GetLastError()));
        CloseHandle(process);
        return false;
    }
    DWORD status = 0;
    if (!GetExitCodeProcess(process, &status)) {
        nob_log(ERROR, "could not read test exit code: %s",
                win32_error_message(GetLastError()));
        CloseHandle(process);
        return false;
    }
    CloseHandle(process);
    *exit_code = (int)status;
    return true;
#else
    uint64_t deadline = nanos_since_unspecified_epoch() +
                        timeout_ms * 1000ULL * 1000ULL;
    for (;;) {
        int status = 0;
        pid_t result = waitpid(process, &status, WNOHANG);
        if (result < 0) {
            nob_log(ERROR, "could not wait for test process: %s",
                    strerror(errno));
            return false;
        }
        if (result == process) {
            if (WIFEXITED(status)) *exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) *exit_code = 128 + WTERMSIG(status);
            else *exit_code = 1;
            return true;
        }
        if (nanos_since_unspecified_epoch() >= deadline) {
            kill(process, SIGKILL);
            waitpid(process, NULL, 0);
            *exit_code = 124;
            return true;
        }
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 10 * 1000 * 1000};
        nanosleep(&pause, NULL);
    }
#endif
}

static bool run_captured(Cmd *command, const char *stdin_path,
                         const char *stdout_path, const char *stderr_path,
                         int *exit_code) {
    Procs processes = {0};
    Nob_Log_Level previous_level = minimal_log_level;
    minimal_log_level = WARNING;
    bool started = cmd_run(command, .async = &processes,
                           .max_procs = SIZE_MAX,
                           .stdin_path = stdin_path,
                           .stdout_path = stdout_path,
                           .stderr_path = stderr_path);
    minimal_log_level = previous_level;
    if (!started || processes.count != 1) {
        da_free(processes);
        return false;
    }
    Proc process = processes.items[0];
    da_free(processes);
    return wait_for_process(process, 10000, exit_code);
}

static bool read_normalized(const char *path, String_Builder *normalized) {
    String_Builder input = {0};
    if (!read_entire_file(path, &input)) return false;
    for (size_t i = 0; i < input.count; ++i) {
        if (input.items[i] == '\r') {
            if (i + 1 < input.count && input.items[i + 1] == '\n') ++i;
            sb_append(normalized, '\n');
        } else {
            sb_append(normalized, input.items[i]);
        }
    }
    sb_append_null(normalized);
    da_free(input);
    return true;
}

static bool buffers_equal(const String_Builder *left,
                          const String_Builder *right) {
    size_t left_count = left->count > 0 ? left->count - 1 : 0;
    size_t right_count = right->count > 0 ? right->count - 1 : 0;
    return left_count == right_count &&
           memcmp(left->items, right->items, left_count) == 0;
}

static void print_test_streams(const String_Builder *stdout_text,
                               const String_Builder *stderr_text) {
    fprintf(stderr, "--- stdout ---\n%s\n",
            stdout_text->items ? stdout_text->items : "");
    fprintf(stderr, "--- stderr ---\n%s\n",
            stderr_text->items ? stderr_text->items : "");
    fprintf(stderr, "--- end streams ---\n");
}

static int compare_names(const void *left, const void *right) {
    const char *const *left_name = left;
    const char *const *right_name = right;
    return strcmp(*left_name, *right_name);
}

static bool test_matches_filter(const Build_Config *config,
                                const char *name) {
    return !config->test_filter || strstr(name, config->test_filter) != NULL;
}

static bool run_toy_case(const Build_Config *config, const char *root,
                         const char *filename) {
    enum { CASE_TEST, CASE_FAIL, CASE_OUTPUT } kind;
    if (starts_with(filename, "test_")) kind = CASE_TEST;
    else if (starts_with(filename, "fail_")) kind = CASE_FAIL;
    else if (starts_with(filename, "output_")) kind = CASE_OUTPUT;
    else return true;

    const char *name = temp_sprintf("%.*s", (int)(strlen(filename) - 4),
                                    filename);
    if (!test_matches_filter(config, name)) return true;

    const char *work_relative = temp_sprintf("%s/test-work/%s",
                                             config->build_dir, name);
    const char *work_absolute = temp_sprintf("%s/%s", root, work_relative);
    Nob_Log_Level previous_level = minimal_log_level;
    minimal_log_level = WARNING;
    bool prepared = remove_tree(work_relative) && ensure_directory(work_relative);
    minimal_log_level = previous_level;
    if (!prepared) {
        return false;
    }

    const char *source = temp_sprintf("tests/toy/%s", filename);
    minimal_log_level = WARNING;
    bool copied = copy_file(source,
                            temp_sprintf("%s/%s", work_relative, filename)) &&
                  copy_file("tests/toy/testlib.toy",
                            temp_sprintf("%s/testlib.toy", work_relative));
    minimal_log_level = previous_level;
    if (!copied) {
        return false;
    }

    const char *toy_absolute = temp_sprintf("%s/%s", root, config->toy_exe);
    if (!set_current_dir(work_absolute)) return false;
    Cmd command = {0};
    cmd_append(&command, toy_absolute, "--file", "testlib.toy",
               "--file", filename, "toy-test-argument");
    int exit_code = 0;
    bool ran = run_captured(&command, NULL, "stdout.txt", "stderr.txt",
                            &exit_code);
    bool restored = set_current_dir(root);
    if (!ran || !restored) return false;

    String_Builder stdout_text = {0};
    String_Builder stderr_text = {0};
    bool ok = read_normalized(temp_sprintf("%s/stdout.txt", work_relative),
                              &stdout_text) &&
              read_normalized(temp_sprintf("%s/stderr.txt", work_relative),
                              &stderr_text);
    if (!ok) goto done;

    if (kind == CASE_TEST) {
        ok = exit_code == 0;
    } else if (kind == CASE_FAIL) {
        const char *expected_path = temp_sprintf("tests/toy/%s.stderr", name);
        String_Builder expected = {0};
        ok = exit_code == 1 && read_normalized(expected_path, &expected);
        while (ok && expected.count > 1 &&
               expected.items[expected.count - 2] == '\n') {
            expected.items[--expected.count - 1] = '\0';
        }
        if (ok) ok = expected.count > 1 &&
                     strstr(stderr_text.items, expected.items) != NULL;
        da_free(expected);
    } else {
        const char *expected_path = temp_sprintf("tests/toy/%s.stdout", name);
        String_Builder expected = {0};
        ok = exit_code == 0 && stderr_text.count == 1 &&
             read_normalized(expected_path, &expected) &&
             buffers_equal(&stdout_text, &expected);
        da_free(expected);
    }

done:
    fprintf(stderr, "[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        fprintf(stderr, "expected %s behavior; exit code was %d\n",
                kind == CASE_TEST ? "successful" :
                kind == CASE_FAIL ? "failure" : "golden-output",
                exit_code);
        print_test_streams(&stdout_text, &stderr_text);
    }
    da_free(stdout_text);
    da_free(stderr_text);
    return ok;
}

static bool run_debug_protocol_test(const Build_Config *config,
                                    const char *root) {
    const char *name = "test_debug_protocol_transport";
    if (!test_matches_filter(config, name)) return true;

    const char *work_relative = temp_sprintf("%s/test-work/%s",
                                             config->build_dir, name);
    const char *work_absolute = temp_sprintf("%s/%s", root, work_relative);
    Nob_Log_Level previous_level = minimal_log_level;
    minimal_log_level = WARNING;
    bool prepared = remove_tree(work_relative) && ensure_directory(work_relative);
    minimal_log_level = previous_level;
    if (!prepared) {
        return false;
    }
    const char commands[] =
        "clear-breakpoints\nbreak 4\ncontinue\nstep\ncontinue\n";
    if (!write_entire_file(temp_sprintf("%s/commands.txt", work_relative),
                           commands, sizeof(commands) - 1)) {
        return false;
    }

    const char *toy_absolute = temp_sprintf("%s/%s", root, config->toy_exe);
    const char *source_absolute = temp_sprintf(
        "%s/tests/toy/test_debug_protocol.toy", root);
    if (!set_current_dir(work_absolute)) return false;
    Cmd command = {0};
    cmd_append(&command, toy_absolute, "--debug-protocol", "--file",
               source_absolute);
    int exit_code = 0;
    bool ran = run_captured(&command, "commands.txt", "stdout.txt",
                            "stderr.txt", &exit_code);
    bool restored = set_current_dir(root);
    if (!ran || !restored) return false;

    String_Builder stdout_text = {0};
    String_Builder stderr_text = {0};
    bool ok = exit_code == 0 &&
              read_normalized(temp_sprintf("%s/stdout.txt", work_relative),
                              &stdout_text) &&
              read_normalized(temp_sprintf("%s/stderr.txt", work_relative),
                              &stderr_text);
    const char *expected[] = {
        "\"event\":\"stopped\",\"reason\":\"entry\"",
        "\"event\":\"stopped\",\"reason\":\"breakpoint\"",
        "\"line\":4",
        "\"value\":\"0\"",
        "\"event\":\"terminated\",\"exitCode\":0",
    };
    for (size_t i = 0; ok && i < ARRAY_LEN(expected); ++i) {
        ok = strstr(stdout_text.items, expected[i]) != NULL;
    }
    fprintf(stderr, "[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) print_test_streams(&stdout_text, &stderr_text);
    da_free(stdout_text);
    da_free(stderr_text);
    return ok;
}

typedef struct {
    const char *name;
    const char *path;
    int exit_code;
    const char *stdout_text;
    const char *diagnostic;
} Package_Test_Case;

static const Package_Test_Case package_test_cases[] = {
    {"test_package_basic", "tests/packages/basic/app", 0, "40\n42\n", NULL},
    {"test_package_core_ffi", "tests/packages/core-ffi", 0, "true\ntrue\n", NULL},
    {"fail_package_private", "tests/packages/private/app", 1, NULL,
     "undefined word 'secrets.hidden'"},
    {"fail_package_cycle", "tests/packages/cycle/app", 1, NULL,
     "cyclic package dependency"},
    {"fail_package_top_level", "tests/packages/top-level", 1, NULL,
     "package-level code may only declare imports, definitions, or privacy"},
    {"fail_package_inconsistent", "tests/packages/inconsistent", 1, NULL,
     "all files in a directory must declare the same package"},
    {"fail_package_missing_main", "tests/packages/missing-main", 1, NULL,
     "must define a public 'main' word"},
    {"fail_package_wrong_name", "tests/packages/wrong-name", 1, NULL,
     "must be named 'main'"},
    {"fail_package_unknown_prefix", "tests/packages/unknown-prefix", 1, NULL,
     "unknown package path prefix"},
    {"fail_package_core_escape", "tests/packages/core-escape", 1, NULL,
     "invalid core package path"},
    {"fail_package_nontransitive", "tests/packages/nontransitive/app", 1,
     NULL, "undefined word 'leaf.value'"},
};

static bool run_package_case(const Build_Config *config, const char *root,
                             const Package_Test_Case *test) {
    const char *toy = temp_sprintf("%s/%s", root, config->toy_exe);
    const char *package = temp_sprintf("%s/%s", root, test->path);
    const char *stdout_path = temp_sprintf("%s/test-work/%s.stdout",
                                           config->build_dir, test->name);
    const char *stderr_path = temp_sprintf("%s/test-work/%s.stderr",
                                           config->build_dir, test->name);
    Cmd command = {0};
    cmd_append(&command, toy, package);
    int exit_code = 0;
    bool ran = run_captured(&command, NULL, stdout_path, stderr_path,
                            &exit_code);
    String_Builder output = {0};
    String_Builder diagnostic = {0};
    bool ok = ran && read_normalized(stdout_path, &output) &&
              read_normalized(stderr_path, &diagnostic) &&
              exit_code == test->exit_code;
    if (ok && test->stdout_text) {
        ok = strcmp(output.items, test->stdout_text) == 0 &&
             diagnostic.count == 1;
    }
    if (ok && test->diagnostic) {
        ok = strstr(diagnostic.items, test->diagnostic) != NULL;
    }
    fprintf(stderr, "[%s] %s\n", ok ? "PASS" : "FAIL", test->name);
    if (!ok) print_test_streams(&output, &diagnostic);
    da_free(output);
    da_free(diagnostic);
    return ok;
}

static bool run_toy_tests(const Build_Config *config, const char *root,
                          size_t *selected_out) {
    File_Paths entries = {0};
    if (!read_entire_dir("tests/toy", &entries)) return false;
    qsort(entries.items, entries.count, sizeof(entries.items[0]), compare_names);

    size_t selected = 0;
    bool ok = true;
    for (size_t i = 0; i < entries.count; ++i) {
        const char *filename = entries.items[i];
        bool is_case = ends_with(filename, ".toy") &&
                       (starts_with(filename, "test_") ||
                        starts_with(filename, "fail_") ||
                        starts_with(filename, "output_"));
        if (!is_case) continue;
        const char *name = temp_sprintf("%.*s",
                                        (int)(strlen(filename) - 4), filename);
        if (!test_matches_filter(config, name)) continue;
        ++selected;
        if (!run_toy_case(config, root, filename)) ok = false;
    }
    if (test_matches_filter(config, "test_debug_protocol_transport")) {
        ++selected;
        if (!run_debug_protocol_test(config, root)) ok = false;
    }
    for (size_t i = 0; i < ARRAY_LEN(package_test_cases); ++i) {
        const Package_Test_Case *test = &package_test_cases[i];
        if (!test_matches_filter(config, test->name)) continue;
        ++selected;
        if (!run_package_case(config, root, test)) ok = false;
    }
    da_free(entries);
    *selected_out = selected;
    return ok;
}

static bool build_c_tests(const Build_Config *config,
                          Compile_Commands *compile_commands,
                          File_Paths *executables) {
    File_Paths headers = {0};
    Procs processes = {0};
    if (!collect_header_dependencies(config, &headers)) return false;

    bool ok = true;
    for (size_t i = 0; ok && i < ARRAY_LEN(core_c_tests); ++i) {
        const char *source = core_c_tests[i];
        const char *name = path_name(source);
        name = temp_sprintf("%.*s", (int)(strlen(name) - 2), name);
        if (!test_matches_filter(config, name)) continue;
        const char *object = object_path(config, source);
        const char *executable = temp_sprintf("%s/tests/%s%s",
                                              config->build_dir, name,
                                              TOY_EXE_SUFFIX);
        ok = schedule_compile(config, source, object, &headers,
                              compile_commands, &processes);
        if (ok) {
            da_append(executables, executable);
            da_append(executables, object);
        }
    }
    if (!procs_flush(&processes)) ok = false;

    for (size_t i = 0; ok && i + 1 < executables->count; i += 2) {
        File_Paths objects = {0};
        da_append(&objects, executables->items[i + 1]);
        ok = link_executable(config, executables->items[i], &objects, true);
        da_free(objects);
    }
    da_free(processes);
    da_free(headers);
    return ok;
}

static bool build_native_loader_test(const Build_Config *config,
                                     Compile_Commands *compile_commands,
                                     const char **loader_executable) {
    *loader_executable = NULL;
    if (!test_matches_filter(config, "test_native_loader")) return true;

    const char *plugin_source = "tests/c/test_native_plugin.c";
    const char *bad_plugin_source = "tests/c/test_native_bad_plugin.c";
    const char *loader_source = "tests/c/test_native_loader.c";
    const char *plugin_object = object_path(config, plugin_source);
    const char *bad_plugin_object = object_path(config, bad_plugin_source);
    const char *loader_object = object_path(config, loader_source);
    const char *plugin_directory = temp_sprintf(
        "%s/plugin", config->test_package_dir);
    const char *bad_plugin_directory = temp_sprintf(
        "%s/bad", config->test_package_dir);
    if (!ensure_directory(plugin_directory) ||
        !ensure_directory(bad_plugin_directory)) {
        return false;
    }
    const char *plugin_file = temp_sprintf(
        "toy_plugin%s", TOY_SHARED_SUFFIX_VALUE);
    const char *bad_plugin_file = temp_sprintf(
        "toy_bad%s", TOY_SHARED_SUFFIX_VALUE);
    const char *plugin_library = temp_sprintf(
        "%s/%s", plugin_directory, plugin_file);
    const char *bad_plugin_library = temp_sprintf(
        "%s/%s", bad_plugin_directory, bad_plugin_file);
    if (!write_package_manifest(plugin_directory, "plugin", plugin_file) ||
        !write_package_manifest(bad_plugin_directory, "bad",
                                bad_plugin_file)) {
        return false;
    }
    *loader_executable = temp_sprintf("%s/tests/test_native_loader%s",
                                      config->build_dir, TOY_EXE_SUFFIX);

    File_Paths headers = {0};
    Procs processes = {0};
    bool ok = collect_header_dependencies(config, &headers);
    if (ok) {
        ok = schedule_compile_ex(config, plugin_source, plugin_object,
                                 &headers, compile_commands, &processes, true);
    }
    if (ok) {
        ok = schedule_compile_ex(config, bad_plugin_source, bad_plugin_object,
                                 &headers, compile_commands, &processes, true);
    }
    if (ok) {
        ok = schedule_compile(config, loader_source, loader_object, &headers,
                              compile_commands, &processes);
    }
    if (!procs_flush(&processes)) ok = false;

    File_Paths objects = {0};
    if (ok) {
        da_append(&objects, plugin_object);
        ok = link_shared_package(config, plugin_library, &objects, false);
        objects.count = 0;
    }
    if (ok) {
        da_append(&objects, bad_plugin_object);
        ok = link_shared_package(config, bad_plugin_library, &objects, false);
        objects.count = 0;
    }
    if (ok) {
        da_append(&objects, loader_object);
        ok = link_executable(config, *loader_executable, &objects, true);
    }

    da_free(objects);
    da_free(processes);
    da_free(headers);
    return ok;
}

static const char *named_test_object(const Build_Config *config,
                                     const char *name) {
#ifdef _WIN32
    return temp_sprintf("%s/%s.obj", config->object_dir, name);
#else
    return temp_sprintf("%s/%s.o", config->object_dir, name);
#endif
}

static bool build_bindgen_test(const Build_Config *config,
                               Compile_Commands *compile_commands,
                               const char **executable_out) {
    *executable_out = NULL;
    if (!test_matches_filter(config, "test_bindgen_package")) return true;

    const char *generated = temp_sprintf(
        "%s/generated/test_generated_binding.c", config->build_dir);
    if (!run_generator(config, "tests/bindings/test_bindgen.json",
                       generated, "bindgen")) {
        return false;
    }

    File_Paths headers = {0};
    File_Paths includes = {0};
    File_Paths definitions = {0};
    File_Paths shared_objects = {0};
    File_Paths test_objects = {0};
    Procs processes = {0};
    da_append(&includes, "tests/c");
    da_append(&definitions, "TEST_BINDGEN_FIXTURE_BUILD");
    const char *generated_object = named_test_object(
        config, "test_generated_binding");
    const char *fixture_object = named_test_object(
        config, "test_bindgen_fixture");
    const char *test_object = named_test_object(config, "test_bindgen_package");
    da_append(&shared_objects, generated_object);
    da_append(&shared_objects, fixture_object);
    da_append(&test_objects, test_object);

    bool ok = collect_header_dependencies(config, &headers);
    if (ok) da_append(&headers, "tests/c/test_bindgen_fixture.h");
    if (ok) {
        ok = schedule_compile_options(
            config, generated, generated_object, &headers, compile_commands,
            &processes, true, &includes, &definitions);
    }
    if (ok) {
        ok = schedule_compile_options(
            config, "tests/c/test_bindgen_fixture.c", fixture_object,
            &headers, compile_commands, &processes, true, &includes,
            &definitions);
    }
    if (ok) {
        ok = schedule_compile(config, "tests/c/test_bindgen_package.c",
                              test_object, &headers, compile_commands,
                              &processes);
    }
    if (!procs_flush(&processes)) ok = false;
    const char *package_directory = temp_sprintf(
        "%s/bindgen", config->test_package_dir);
    if (!ensure_directory(package_directory)) return false;
    const char *extension_file = temp_sprintf(
        "toy_bindgen%s", TOY_SHARED_SUFFIX_VALUE);
    const char *package_library = temp_sprintf("%s/%s", package_directory,
                                               extension_file);
    if (!write_package_manifest(package_directory, "bindgen",
                                extension_file)) {
        return false;
    }
    if (ok) {
        ok = link_shared_package(config, package_library, &shared_objects, false);
    }
    *executable_out = temp_sprintf("%s/tests/test_bindgen_package%s",
                                   config->build_dir, TOY_EXE_SUFFIX);
    if (ok) {
        ok = link_executable(config, *executable_out, &test_objects, true);
    }
    da_free(processes);
    da_free(test_objects);
    da_free(shared_objects);
    da_free(definitions);
    da_free(includes);
    da_free(headers);
    return ok;
}

static bool run_c_test(const Build_Config *config, const char *root,
                       const char *executable, const char *argument) {
    const char *name = path_name(executable);
    const char *absolute = temp_sprintf("%s/%s", root, executable);
    const char *stdout_path = temp_sprintf("%s/test-work/c.stdout",
                                           config->build_dir);
    const char *stderr_path = temp_sprintf("%s/test-work/c.stderr",
                                           config->build_dir);
    Cmd command = {0};
    cmd_append(&command, absolute);
    if (argument) cmd_append(&command, argument);
    int exit_code = 0;
    bool ran = run_captured(&command, NULL, stdout_path, stderr_path,
                            &exit_code);
    bool passed = ran && exit_code == 0;
    fprintf(stderr, "[%s] %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed) {
        String_Builder stdout_text = {0};
        String_Builder stderr_text = {0};
        if (read_normalized(stdout_path, &stdout_text) &&
            read_normalized(stderr_path, &stderr_text)) {
            print_test_streams(&stdout_text, &stderr_text);
        }
        da_free(stdout_text);
        da_free(stderr_text);
    }
    return passed;
}

static bool run_binding_generator_test(const Build_Config *config) {
    const char *name = "test_binding_generator_js";
    if (!test_matches_filter(config, name)) return true;
    if (!program_on_path("node")) {
        fprintf(stderr, "[FAIL] %s (Node.js was not found)\n", name);
        return false;
    }
    Cmd command = {0};
    cmd_append(&command, "node", "tests/tools/test_generate_binding.js");
    const char *stdout_path = temp_sprintf("%s/test-work/bindgen-js.stdout",
                                           config->build_dir);
    const char *stderr_path = temp_sprintf("%s/test-work/bindgen-js.stderr",
                                           config->build_dir);
    int exit_code = 0;
    bool ran = run_captured(&command, NULL, stdout_path, stderr_path,
                            &exit_code);
    bool passed = ran && exit_code == 0;
    fprintf(stderr, "[%s] %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed) {
        String_Builder stdout_text = {0};
        String_Builder stderr_text = {0};
        if (read_normalized(stdout_path, &stdout_text) &&
            read_normalized(stderr_path, &stderr_text)) {
            print_test_streams(&stdout_text, &stderr_text);
        }
        da_free(stdout_text);
        da_free(stderr_text);
    }
    return passed;
}

static bool run_ffi_integration_test(const Build_Config *config,
                                     const char *root,
                                     Compile_Commands *compile_commands) {
    File_Paths headers = {0};
    File_Paths fixture_objects = {0};
    File_Paths test_objects = {0};
    Procs processes = {0};
    const char *fixture_object = named_test_object(config, "test_ffi_fixture");
    const char *test_object = named_test_object(config, "test_ffi_package");
    da_append(&fixture_objects, fixture_object);
    da_append(&test_objects, test_object);

    bool ok = collect_header_dependencies(config, &headers);
    if (ok) {
        ok = schedule_compile_ex(config, "tests/c/test_ffi_fixture.c",
                                 fixture_object, &headers, compile_commands,
                                 &processes, true);
    }
    if (ok) {
        ok = schedule_compile(config, "tests/c/test_ffi_package.c",
                              test_object, &headers, compile_commands,
                              &processes);
    }
    if (!procs_flush(&processes)) ok = false;

    const char *fixture = temp_sprintf("%s/toy_ffi_fixture%s",
                                       config->extension_artifact_dir,
                                       TOY_SHARED_SUFFIX_VALUE);
    if (ok) {
        ok = link_shared_package(config, fixture, &fixture_objects, false);
    }
    const char *executable = temp_sprintf("%s/tests/test_ffi_package%s",
                                          config->build_dir, TOY_EXE_SUFFIX);
    if (ok) {
        ok = link_executable(config, executable, &test_objects, true);
    }

    if (ok) {
        Cmd command = {0};
        cmd_append(&command, temp_sprintf("%s/%s", root, executable),
                   temp_sprintf("%s/%s", root, config->core_package_dir),
                   temp_sprintf("%s/%s", root, fixture));
        const char *stdout_path = temp_sprintf("%s/test-work/ffi.stdout",
                                               config->build_dir);
        const char *stderr_path = temp_sprintf("%s/test-work/ffi.stderr",
                                               config->build_dir);
        int exit_code = 0;
        bool ran = run_captured(&command, NULL, stdout_path, stderr_path,
                                &exit_code);
        ok = ran && exit_code == 0;
        fprintf(stderr, "[%s] test_ffi_package\n", ok ? "PASS" : "FAIL");
        if (!ok) {
            String_Builder stdout_text = {0};
            String_Builder stderr_text = {0};
            if (read_normalized(stdout_path, &stdout_text) &&
                read_normalized(stderr_path, &stderr_text)) {
                print_test_streams(&stdout_text, &stderr_text);
            }
            da_free(stdout_text);
            da_free(stderr_text);
        }
    }

    da_free(processes);
    da_free(test_objects);
    da_free(fixture_objects);
    da_free(headers);
    return ok;
}

static bool run_c_tests(const Build_Config *config, const char *root,
                        const File_Paths *executables) {
    bool ok = true;
    for (size_t i = 0; i + 1 < executables->count; i += 2) {
        const char *executable = executables->items[i];
        if (!run_c_test(config, root, executable, NULL)) ok = false;
    }
    return ok;
}

NOBDEF bool run_all_tests(const Build_Config *config, const char *root,
                          Compile_Commands *compile_commands) {
    File_Paths c_test_artifacts = {0};
    bool build_ok = build_c_tests(config, compile_commands, &c_test_artifacts);
    const char *native_loader = NULL;
    if (build_ok) {
        build_ok = build_native_loader_test(config, compile_commands,
                                            &native_loader);
    }
    const char *bindgen_test = NULL;
    if (build_ok) {
        build_ok = build_bindgen_test(config, compile_commands,
                                      &bindgen_test);
    }
    if (!build_ok) {
        da_free(c_test_artifacts);
        return false;
    }
    size_t toy_test_count = 0;
    bool toy_ok = run_toy_tests(config, root, &toy_test_count);
    bool c_ok = run_c_tests(config, root, &c_test_artifacts);
    size_t c_test_count = c_test_artifacts.count / 2;
    if (native_loader) {
        const char *package_directory = temp_sprintf(
            "%s/%s", root, config->test_package_dir);
        if (!run_c_test(config, root, native_loader, package_directory)) {
            c_ok = false;
        }
        ++c_test_count;
    }
    if (bindgen_test) {
        const char *package_directory = temp_sprintf(
            "%s/%s", root, config->test_package_dir);
        if (!run_c_test(config, root, bindgen_test, package_directory)) {
            c_ok = false;
        }
        ++c_test_count;
    }
    bool generator_ok = run_binding_generator_test(config);
    size_t generator_test_count = test_matches_filter(
        config, "test_binding_generator_js") ? 1 : 0;
    bool ffi_ok = true;
    size_t ffi_test_count = 0;
    if (config->test_filter &&
        strstr("optional_ffi", config->test_filter) != NULL) {
        ffi_ok = run_ffi_integration_test(config, root, compile_commands);
        ffi_test_count = 1;
    }
    bool ok = toy_ok && c_ok && generator_ok && ffi_ok;
    size_t test_count = toy_test_count + c_test_count + generator_test_count +
                        ffi_test_count;
    if (test_count == 0) {
        nob_log(ERROR, "no tests matched filter '%s'",
                config->test_filter ? config->test_filter : "");
        ok = false;
    } else if (ok) {
        nob_log(INFO, "%zu tests passed", test_count);
    }
    da_free(c_test_artifacts);
    return ok;
}

#endif  // NOB_IMPLEMENTATION

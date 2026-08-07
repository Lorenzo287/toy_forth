#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "tf_exec.h"

#include <ctype.h>
#include <errno.h>  // IWYU pragma: keep
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tf_alloc.h"
#include "tf_native_loader.h"
#include "tf_parser.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#endif

typedef struct {
    char *path;
    tf_obj *program;
} package_source;

typedef enum {
    PACKAGE_DECL_IMPORT,
    PACKAGE_DECL_IMPORT_AS,
    PACKAGE_DECL_DEF,
    PACKAGE_DECL_PRIVATE,
} package_decl_kind;

typedef struct {
    package_decl_kind kind;
    tf_obj *first;
    tf_obj *second;
} package_decl;

typedef struct {
    char *name;
    char *extension;
    bool present;
} package_manifest;

typedef struct {
    package_source *sources;
    size_t source_len;
    size_t source_cap;
    package_decl *decls;
    size_t decl_len;
    size_t decl_cap;
    char *name;
    size_t name_len;
    package_manifest manifest;
} package_scan;

static bool path_is_absolute(const char *path) {
    if (!path || path[0] == '\0') return false;
#ifdef _WIN32
    return (isalpha((unsigned char)path[0]) && path[1] == ':' &&
            (path[2] == '/' || path[2] == '\\')) ||
           path[0] == '\\' || path[0] == '/';
#else
    return path[0] == '/';
#endif
}

static char *join_path(const char *directory, const char *name) {
    size_t directory_len = strlen(directory);
    size_t name_len = strlen(name);
    bool separator = directory_len > 0 &&
                     directory[directory_len - 1] != '/' &&
                     directory[directory_len - 1] != '\\';
    char *result = tf_xmalloc(directory_len + (separator ? 1 : 0) +
                              name_len + 1);
    memcpy(result, directory, directory_len);
    size_t out = directory_len;
    if (separator) result[out++] = '/';
    memcpy(result + out, name, name_len + 1);
    return result;
}

static bool is_directory(const char *path) {
    struct stat info;
    if (stat(path, &info) != 0) return false;
#ifdef _WIN32
    return (info.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(info.st_mode);
#endif
}

static bool is_regular_file(const char *path) {
    struct stat info;
    if (stat(path, &info) != 0) return false;
#ifdef _WIN32
    return (info.st_mode & _S_IFREG) != 0;
#else
    return S_ISREG(info.st_mode);
#endif
}

static char *canonical_directory(tf_ctx *ctx, const char *path,
                                 const char *request,
                                 const char *base_kind, const char *base) {
    char *resolved = NULL;
#ifdef _WIN32
    DWORD needed = GetFullPathNameA(path, 0, NULL, NULL);
    if (needed > 0) {
        resolved = tf_xmalloc(needed);
        DWORD written = GetFullPathNameA(path, needed, resolved, NULL);
        if (written == 0 || written >= needed) {
            free(resolved);
            resolved = NULL;
        }
    }
#else
    resolved = realpath(path, NULL);
#endif
    for (char *cursor = resolved; cursor && *cursor; cursor++) {
        if (*cursor == '\\') *cursor = '/';
    }
    if (!resolved || !is_directory(resolved)) {
        if (base_kind && base) {
            tf_ctx_runtime_errorf(
                ctx,
                "package import '%s' resolved from %s '%s' to '%s', "
                "which is not a directory\n",
                request, base_kind, base, resolved ? resolved : path);
        } else {
            tf_ctx_runtime_errorf(
                ctx,
                "package import '%s' resolved to '%s', which is not a directory\n",
                request, resolved ? resolved : path);
        }
        free(resolved);
        return NULL;
    }
    size_t len = strlen(resolved);
    while (len > 1 && resolved[len - 1] == '/') resolved[--len] = '\0';
    return resolved;
}

static bool safe_core_request(const char *path) {
    if (!path || path[0] == '\0' || path[0] == '/' || path[0] == '\\') {
        return false;
    }
    if (strchr(path, ':')) return false;
    const char *segment = path;
    while (*segment) {
        const char *end = segment;
        while (*end && *end != '/' && *end != '\\') end++;
        size_t len = (size_t)(end - segment);
        if (len == 0 || (len == 1 && segment[0] == '.') ||
            (len == 2 && segment[0] == '.' && segment[1] == '.')) {
            return false;
        }
        segment = *end ? end + 1 : end;
    }
    return true;
}

static char *resolve_request(tf_ctx *ctx, const char *request,
                             size_t owner_package_index) {
    if (!request || request[0] == '\0') {
        tf_ctx_runtime_errorf(ctx, "package import path must not be empty\n");
        return NULL;
    }

    char *candidate = NULL;
    const char *base = NULL;
    const char *base_kind = NULL;
    if (strncmp(request, "core:", 5) == 0) {
        const char *relative = request + 5;
        if (!ctx->core_package_path) {
            tf_ctx_runtime_errorf(ctx,
                                  "cannot resolve '%s': no core package path is configured\n",
                                  request);
            return NULL;
        }
        if (!safe_core_request(relative)) {
            tf_ctx_runtime_errorf(ctx, "invalid core package path '%s'\n",
                                  request);
            return NULL;
        }
        base = ctx->core_package_path;
        base_kind = "core package directory";
        candidate = join_path(base, relative);
    } else if (strchr(request, ':') && !path_is_absolute(request)) {
        tf_ctx_runtime_errorf(ctx, "unknown package path prefix in '%s'\n",
                              request);
        return NULL;
    } else if (path_is_absolute(request)) {
        candidate = tf_xstrdup(request);
    } else {
        const tf_package *owner = tf_package_get(ctx, owner_package_index);
        const char *source_base =
            owner_package_index == TF_ROOT_PACKAGE && ctx->current_span.source
                ? tf_source_file_import_base(ctx->current_span.source)
                : NULL;
        if (source_base) {
            base = source_base;
            base_kind = "source directory";
        } else if (owner && owner->path) {
            base = owner->path;
            base_kind = "package directory";
        } else {
            base = ".";
            base_kind = "working directory";
        }
        candidate = join_path(base, request);
    }

    char *resolved = canonical_directory(ctx, candidate, request, base_kind,
                                         base);
    free(candidate);
    return resolved;
}

static char *read_text_file(tf_ctx *ctx, const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        tf_ctx_runtime_errorf(ctx, "failed to open package file '%s'\n", path);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        tf_ctx_runtime_errorf(ctx, "failed to seek package file '%s'\n", path);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        tf_ctx_runtime_errorf(ctx, "failed to read package file '%s'\n", path);
        return NULL;
    }
    char *text = tf_xmalloc((size_t)size + 1);
    size_t read = fread(text, 1, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) {
        free(text);
        tf_ctx_runtime_errorf(ctx, "failed to read package file '%s'\n", path);
        return NULL;
    }
    text[read] = '\0';
    return text;
}

#ifndef _WIN32
static bool ends_with(const char *value, const char *suffix) {
    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    return value_len >= suffix_len &&
           memcmp(value + value_len - suffix_len, suffix, suffix_len) == 0;
}
#endif

static int compare_strings(const void *left, const void *right) {
    const char *const *a = left;
    const char *const *b = right;
    return strcmp(*a, *b);
}

static bool append_name(char ***names, size_t *len, size_t *cap,
                        const char *name) {
    if (*len >= *cap) {
        *cap = *cap ? *cap * 2 : 8;
        *names = tf_xrealloc(*names, sizeof(char *) * *cap);
    }
    (*names)[(*len)++] = tf_xstrdup(name);
    return true;
}

static bool collect_source_names(tf_ctx *ctx, const char *directory,
                                 char ***names, size_t *len) {
    size_t cap = 0;
#ifdef _WIN32
    (void)ctx;
    char *pattern = join_path(directory, "*.toy");
    WIN32_FIND_DATAA data;
    HANDLE search = FindFirstFileA(pattern, &data);
    free(pattern);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                append_name(names, len, &cap, data.cFileName);
            }
        } while (FindNextFileA(search, &data));
        FindClose(search);
    }
#else
    DIR *dir = opendir(directory);
    if (!dir) {
        tf_ctx_runtime_errorf(ctx, "failed to inspect package directory '%s'\n",
                              directory);
        return false;
    }
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (!ends_with(entry->d_name, ".toy")) continue;
        char *path = join_path(directory, entry->d_name);
        bool regular = is_regular_file(path);
        free(path);
        if (regular) append_name(names, len, &cap, entry->d_name);
    }
    closedir(dir);
#endif
    if (*len > 1) qsort(*names, *len, sizeof(char *), compare_strings);
    return true;
}

static char *trim(char *text) {
    while (isspace((unsigned char)*text)) text++;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return text;
}

static char *manifest_value(char *text) {
    text = trim(text);
    size_t len = strlen(text);
    if (len >= 2 && text[0] == '"' && text[len - 1] == '"') {
        text[len - 1] = '\0';
        return text + 1;
    }
    return text;
}

static bool read_manifest(tf_ctx *ctx, const char *directory,
                          package_manifest *manifest) {
    char *path = join_path(directory, "toy.package");
    if (!is_regular_file(path)) {
        free(path);
        return true;
    }
    char *text = read_text_file(ctx, path);
    if (!text) {
        free(path);
        return false;
    }
    manifest->present = true;

    size_t line_number = 0;
    for (char *line = text; line;) {
        line_number++;
        char *next = strchr(line, '\n');
        if (next) *next++ = '\0';
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        char *content = trim(line);
        if (*content) {
            char *equals = strchr(content, '=');
            if (!equals) {
                tf_ctx_runtime_errorf(ctx,
                                      "invalid package manifest declaration at %s:%zu\n",
                                      path, line_number);
                free(text);
                free(path);
                return false;
            }
            *equals = '\0';
            char *key = trim(content);
            char *value = manifest_value(equals + 1);
            if (*value == '\0') {
                tf_ctx_runtime_errorf(ctx,
                                      "empty package manifest value for '%s' at %s:%zu\n",
                                      key, path, line_number);
                free(text);
                free(path);
                return false;
            }
            char **slot = NULL;
            if (strcmp(key, "name") == 0) slot = &manifest->name;
            else if (strcmp(key, "extension") == 0) {
                slot = &manifest->extension;
            }
            else {
                tf_ctx_runtime_errorf(ctx,
                                      "unknown package manifest key '%s' at %s:%zu\n",
                                      key, path, line_number);
                free(text);
                free(path);
                return false;
            }
            if (*slot) {
                tf_ctx_runtime_errorf(ctx,
                                      "duplicate package manifest key '%s' at %s:%zu\n",
                                      key, path, line_number);
                free(text);
                free(path);
                return false;
            }
            *slot = tf_xstrdup(value);
        }
        line = next;
    }
    free(text);
    free(path);

    if (!manifest->name || !tf_package_name_valid(manifest->name,
                                                   strlen(manifest->name))) {
        tf_ctx_runtime_errorf(ctx,
                              "package manifest requires a valid 'name'\n");
        return false;
    }
    if (!manifest->extension) {
        tf_ctx_runtime_errorf(ctx,
                              "package manifest requires an exact 'extension' library path\n");
        return false;
    }
    return true;
}

static void scan_dispose(package_scan *scan) {
    for (size_t i = 0; i < scan->source_len; i++) {
        free(scan->sources[i].path);
        tf_obj_release(scan->sources[i].program);
    }
    free(scan->sources);
    free(scan->decls);
    free(scan->name);
    free(scan->manifest.name);
    free(scan->manifest.extension);
    memset(scan, 0, sizeof(*scan));
}

static bool call_is(tf_obj *object, const char *name) {
    size_t len = strlen(name);
    return object && object->type == TF_OBJ_TYPE_CALL &&
           object->str.len == len && memcmp(object->str.ptr, name, len) == 0;
}

static void declaration_error(tf_ctx *ctx, tf_obj *object,
                              const char *message) {
    ctx->current_span = object ? object->span : (tf_source_span){0};
    tf_ctx_runtime_errorf(ctx, "%s\n", message);
}

static bool scan_add_source(package_scan *scan, char *path, tf_obj *program) {
    if (scan->source_len >= scan->source_cap) {
        scan->source_cap = scan->source_cap ? scan->source_cap * 2 : 4;
        scan->sources = tf_xrealloc(
            scan->sources, sizeof(package_source) * scan->source_cap);
    }
    scan->sources[scan->source_len++] = (package_source){path, program};
    return true;
}

static bool scan_add_decl(package_scan *scan, package_decl_kind kind,
                          tf_obj *first, tf_obj *second) {
    if (scan->decl_len >= scan->decl_cap) {
        scan->decl_cap = scan->decl_cap ? scan->decl_cap * 2 : 16;
        scan->decls = tf_xrealloc(
            scan->decls, sizeof(package_decl) * scan->decl_cap);
    }
    scan->decls[scan->decl_len++] =
        (package_decl){kind, first, second};
    return true;
}

static bool scan_program(tf_ctx *ctx, package_scan *scan, tf_obj *program) {
    if (program->vector.len < 2 ||
        program->vector.elem[0]->type != TF_OBJ_TYPE_SYMBOL ||
        !call_is(program->vector.elem[1], "package")) {
        declaration_error(ctx, program,
                          "every package source must begin with 'name package");
        return false;
    }

    tf_obj *declared = program->vector.elem[0];
    if (!tf_package_name_valid(declared->str.ptr, declared->str.len)) {
        declaration_error(ctx, declared, "invalid package name");
        return false;
    }
    if (!scan->name) {
        scan->name = tf_xmalloc(declared->str.len + 1);
        memcpy(scan->name, declared->str.ptr, declared->str.len);
        scan->name[declared->str.len] = '\0';
        scan->name_len = declared->str.len;
    } else if (scan->name_len != declared->str.len ||
               memcmp(scan->name, declared->str.ptr, scan->name_len) != 0) {
        declaration_error(ctx, declared,
                          "all files in a directory must declare the same package");
        return false;
    }

    size_t i = 2;
    while (i < program->vector.len) {
        tf_obj *first = program->vector.elem[i];
        if (i + 1 < program->vector.len &&
            first->type == TF_OBJ_TYPE_STR &&
            call_is(program->vector.elem[i + 1], "import")) {
            scan_add_decl(scan, PACKAGE_DECL_IMPORT, first, NULL);
            i += 2;
            continue;
        }
        if (i + 2 < program->vector.len &&
            first->type == TF_OBJ_TYPE_STR &&
            program->vector.elem[i + 1]->type == TF_OBJ_TYPE_SYMBOL &&
            call_is(program->vector.elem[i + 2], "import-as")) {
            scan_add_decl(scan, PACKAGE_DECL_IMPORT_AS, first,
                          program->vector.elem[i + 1]);
            i += 3;
            continue;
        }
        if (i + 2 < program->vector.len &&
            first->type == TF_OBJ_TYPE_SYMBOL &&
            program->vector.elem[i + 1]->type == TF_OBJ_TYPE_VECTOR &&
            call_is(program->vector.elem[i + 2], "def")) {
            if (!tf_package_word_name_valid(first->str.ptr, first->str.len)) {
                declaration_error(ctx, first, "invalid package word name");
                return false;
            }
            scan_add_decl(scan, PACKAGE_DECL_DEF, first,
                          program->vector.elem[i + 1]);
            i += 3;
            continue;
        }
        if (i + 1 < program->vector.len &&
            first->type == TF_OBJ_TYPE_SYMBOL &&
            call_is(program->vector.elem[i + 1], "private")) {
            scan_add_decl(scan, PACKAGE_DECL_PRIVATE, first, NULL);
            i += 2;
            continue;
        }
        declaration_error(
            ctx, first,
            "package-level code may only declare imports, definitions, or privacy");
        return false;
    }
    return true;
}

static bool scan_directory(tf_ctx *ctx, const char *request,
                           const char *directory, package_scan *scan) {
    if (!read_manifest(ctx, directory, &scan->manifest)) return false;

    char **names = NULL;
    size_t name_len = 0;
    if (!collect_source_names(ctx, directory, &names, &name_len)) return false;
    for (size_t i = 0; i < name_len; i++) {
        char *path = join_path(directory, names[i]);
        char *text = read_text_file(ctx, path);
        if (!text) {
            free(path);
            for (size_t j = i; j < name_len; j++) free(names[j]);
            for (size_t j = 0; j < i; j++) free(names[j]);
            free(names);
            return false;
        }
        tf_obj *program = tf_parse_source(ctx, path, text);
        free(text);
        if (!program) {
            free(path);
            for (size_t j = 0; j < name_len; j++) free(names[j]);
            free(names);
            return false;
        }
        scan_add_source(scan, path, program);
        if (!scan_program(ctx, scan, program)) {
            for (size_t j = 0; j < name_len; j++) free(names[j]);
            free(names);
            return false;
        }
    }
    for (size_t i = 0; i < name_len; i++) free(names[i]);
    free(names);

    if (scan->manifest.present) {
        if (scan->name && strcmp(scan->name, scan->manifest.name) != 0) {
            tf_ctx_runtime_errorf(
                ctx,
                "package manifest name '%s' does not match source package '%s'\n",
                scan->manifest.name, scan->name);
            return false;
        }
        if (!scan->name) {
            scan->name = tf_xstrdup(scan->manifest.name);
            scan->name_len = strlen(scan->name);
        }
    }
    if (!scan->name) {
        tf_ctx_runtime_errorf(
            ctx,
            "package import '%s' resolved to directory '%s', but it contains no .toy files or toy.package manifest\n",
            request, directory);
        return false;
    }
    return true;
}

static bool object_c_string(tf_ctx *ctx, tf_obj *object,
                            const char **result) {
    if (strlen(object->str.ptr) != object->str.len) {
        declaration_error(ctx, object,
                          "package import paths cannot contain NUL bytes");
        return false;
    }
    *result = object->str.ptr;
    return true;
}

static tf_ret install_scan(tf_ctx *ctx, package_scan *scan,
                           const char *directory, size_t package_index) {
    for (size_t i = 0; i < scan->source_len; i++) {
        tf_source_file_set_package(scan->sources[i].program->span.source,
                                   package_index);
    }

    for (size_t i = 0; i < scan->decl_len; i++) {
        package_decl *decl = &scan->decls[i];
        if (decl->kind != PACKAGE_DECL_IMPORT &&
            decl->kind != PACKAGE_DECL_IMPORT_AS) {
            continue;
        }
        ctx->current_span = decl->first->span;
        const char *request = NULL;
        if (!object_c_string(ctx, decl->first, &request)) return TF_ERR;
        const char *alias = decl->kind == PACKAGE_DECL_IMPORT_AS
                                ? decl->second->str.ptr
                                : NULL;
        size_t alias_len = decl->kind == PACKAGE_DECL_IMPORT_AS
                               ? decl->second->str.len
                               : 0;
        if (tf_package_load(ctx, request, package_index, alias, alias_len,
                            NULL) != TF_OK) {
            return TF_ERR;
        }
    }

    if (scan->manifest.present) {
        char *extension = path_is_absolute(scan->manifest.extension)
                              ? tf_xstrdup(scan->manifest.extension)
                              : join_path(directory,
                                          scan->manifest.extension);
        tf_ret status =
            tf_native_package_load(ctx, package_index, extension);
        free(extension);
        if (status != TF_OK) return status;
    }

    for (size_t i = 0; i < scan->decl_len; i++) {
        package_decl *decl = &scan->decls[i];
        if (decl->kind != PACKAGE_DECL_DEF) continue;
        ctx->current_span = decl->first->span;
        if (tf_dict_lookup_scoped(ctx, package_index, decl->first->str.ptr,
                                  decl->first->str.len)) {
            tf_ctx_runtime_errorf(ctx, "duplicate package word '%s'\n",
                                  decl->first->str.ptr);
            return TF_ERR;
        }
        if (!tf_dict_set_user_in_package(ctx, package_index, decl->first,
                                         decl->second)) {
            return TF_ERR;
        }
    }

    for (size_t i = 0; i < scan->decl_len; i++) {
        package_decl *decl = &scan->decls[i];
        if (decl->kind != PACKAGE_DECL_PRIVATE) continue;
        ctx->current_span = decl->first->span;
        if (!tf_dict_make_private_in_package(ctx, package_index,
                                             decl->first)) {
            return TF_ERR;
        }
    }
    return TF_OK;
}

static void apply_core_package_docs(tf_ctx *ctx, size_t package_index,
                                    const char *request) {
    const tf_package_doc *docs = tf_core_package_doc_lookup(request);
    const tf_package *package = tf_package_get(ctx, package_index);
    if (!docs || !package || package->name_len != strlen(docs->name) ||
        memcmp(package->name, docs->name, package->name_len) != 0) {
        return;
    }

    for (size_t i = 0; i < docs->word_count; i++) {
        const tf_doc_entry *word = &docs->words[i];
        tf_dict_set_doc_scoped(ctx, package_index, word->name,
                               strlen(word->name), word->stack_effect,
                               word->description);
    }
}

tf_ret tf_package_load(tf_ctx *ctx, const char *request,
                       size_t owner_package_index, const char *alias,
                       size_t alias_len, size_t *package_index_out) {
    if (!ctx || !request || owner_package_index >= ctx->packages.len) {
        return TF_ERR;
    }
    if (alias && !tf_package_name_valid(alias, alias_len)) {
        tf_ctx_runtime_errorf(ctx, "invalid package import name '%.*s'\n",
                              (int)alias_len, alias);
        return TF_ERR;
    }

    char *directory = resolve_request(ctx, request, owner_package_index);
    if (!directory) return TF_ERR;
    size_t existing = tf_package_find_path(ctx, directory);
    if (existing != (size_t)-1) {
        const tf_package *package = tf_package_get(ctx, existing);
        if (package->state == TF_PACKAGE_LOADING) {
            tf_ctx_runtime_errorf(ctx,
                                  "cyclic package dependency involving '%s'\n",
                                  package->name);
            free(directory);
            return TF_ERR;
        }
        if (package->state == TF_PACKAGE_FAILED) {
            tf_ctx_runtime_errorf(ctx,
                                  "package '%s' previously failed to load\n",
                                  package->name);
            free(directory);
            return TF_ERR;
        }
        apply_core_package_docs(ctx, existing, request);
        const char *import_name = alias ? alias : package->name;
        size_t import_len = alias ? alias_len : package->name_len;
        if (!tf_package_import_add(ctx, owner_package_index, import_name,
                                   import_len, existing)) {
            tf_ctx_runtime_errorf(ctx,
                                  "package import name '%.*s' is already bound\n",
                                  (int)import_len, import_name);
            free(directory);
            return TF_ERR;
        }
        if (package_index_out) *package_index_out = existing;
        free(directory);
        return TF_OK;
    }

    package_scan scan = {0};
    if (!scan_directory(ctx, request, directory, &scan)) {
        scan_dispose(&scan);
        free(directory);
        return TF_ERR;
    }

    size_t package_index = tf_package_begin(ctx, scan.name, scan.name_len,
                                            directory);
    tf_ret status = install_scan(ctx, &scan, directory, package_index);
    tf_package_finish(ctx, package_index, status);
    if (status == TF_OK) {
        apply_core_package_docs(ctx, package_index, request);
        const char *import_name = alias ? alias : scan.name;
        size_t import_len = alias ? alias_len : scan.name_len;
        if (!tf_package_import_add(ctx, owner_package_index, import_name,
                                   import_len, package_index)) {
            tf_ctx_runtime_errorf(ctx,
                                  "package import name '%.*s' is already bound\n",
                                  (int)import_len, import_name);
            status = TF_ERR;
        }
    }
    if (status == TF_OK && package_index_out) {
        *package_index_out = package_index;
    }
    scan_dispose(&scan);
    free(directory);
    return status;
}

tf_ret tf_package_run_main(tf_ctx *ctx, const char *path) {
    size_t package_index = (size_t)-1;
    tf_ret status = tf_package_load(ctx, path, TF_ROOT_PACKAGE, NULL, 0,
                                    &package_index);
    if (status != TF_OK) return status;

    const tf_package *package = tf_package_get(ctx, package_index);
    if (!package || strcmp(package->name, "main") != 0) {
        tf_ctx_runtime_errorf(ctx,
                              "executable package must be named 'main'\n");
        return TF_ERR;
    }
    tf_word *main_word = tf_dict_lookup_scoped(ctx, package_index, "main", 4);
    if (!main_word || !main_word->is_public ||
        main_word->type != TF_WORD_USER) {
        tf_ctx_runtime_errorf(ctx,
                              "executable package must define a public 'main' word\n");
        return TF_ERR;
    }

    tf_obj *program = tf_obj_new_vector();
    tf_obj *call = tf_obj_new_call("main", 4);
    tf_obj_set_span(call, main_word->user_impl->span);
    tf_vector_push(program, call);
    status = tf_vm_exec_package(ctx, program, package_index);
    tf_obj_release(program);
    return status;
}

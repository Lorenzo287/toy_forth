#include "tf_exec.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "tf_alloc.h"

void tf_packages_init(tf_ctx *ctx) {
    ctx->packages = (tf_package_table){0};
    ctx->packages.cap = 4;
    ctx->packages.len = 1;
    ctx->packages.entries =
        tf_xcalloc(ctx->packages.cap, sizeof(tf_package));
    ctx->packages.entries[TF_ROOT_PACKAGE].name = tf_xstrdup("");
    ctx->packages.entries[TF_ROOT_PACKAGE].state = TF_PACKAGE_LOADED;

    ctx->package_imports = (tf_package_import_table){0};
    ctx->package_imports.cap = 4;
    ctx->package_imports.entries =
        tf_xcalloc(ctx->package_imports.cap, sizeof(tf_package_import));
    ctx->core_package_path = NULL;
}

void tf_packages_free(tf_ctx *ctx) {
    if (!ctx) return;

    for (size_t i = 0; i < ctx->packages.len; i++) {
        free(ctx->packages.entries[i].name);
        free(ctx->packages.entries[i].path);
    }
    free(ctx->packages.entries);

    for (size_t i = 0; i < ctx->package_imports.len; i++) {
        free(ctx->package_imports.entries[i].name);
    }
    free(ctx->package_imports.entries);
    free(ctx->core_package_path);

    ctx->packages = (tf_package_table){0};
    ctx->package_imports = (tf_package_import_table){0};
    ctx->core_package_path = NULL;
}

bool tf_package_name_valid(const char *name, size_t name_len) {
    if (!name || name_len == 0) return false;
    unsigned char first = (unsigned char)name[0];
    if (!isalpha(first) && first != '_') return false;
    for (size_t i = 1; i < name_len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!isalnum(c) && c != '_' && c != '-') return false;
    }
    return true;
}

bool tf_package_word_name_valid(const char *name, size_t name_len) {
    if (!name || name_len == 0) return false;
    const char *operators = "+-*%<>=!?";
    unsigned char first = (unsigned char)name[0];
    if (!isalpha(first) && first != '_' && !strchr(operators, first)) {
        return false;
    }
    for (size_t i = 1; i < name_len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!isalnum(c) && c != '_' && !strchr(operators, c)) return false;
    }
    return true;
}

static bool path_equal(const char *left, const char *right) {
#ifdef _WIN32
    return _stricmp(left, right) == 0;
#else
    return strcmp(left, right) == 0;
#endif
}

size_t tf_package_find_path(tf_ctx *ctx, const char *path) {
    if (!ctx || !path) return (size_t)-1;
    for (size_t i = 1; i < ctx->packages.len; i++) {
        tf_package *package = &ctx->packages.entries[i];
        if (package->path && path_equal(package->path, path)) return i;
    }
    return (size_t)-1;
}

static size_t package_add(tf_ctx *ctx, const char *name, size_t name_len,
                          const char *path, tf_package_state state) {
    if (ctx->packages.len >= ctx->packages.cap) {
        ctx->packages.cap *= 2;
        ctx->packages.entries =
            tf_xrealloc(ctx->packages.entries,
                        sizeof(tf_package) * ctx->packages.cap);
    }

    size_t index = ctx->packages.len++;
    tf_package *package = &ctx->packages.entries[index];
    package->name = tf_xmalloc(name_len + 1);
    memcpy(package->name, name, name_len);
    package->name[name_len] = '\0';
    package->name_len = name_len;
    package->path = path ? tf_xstrdup(path) : NULL;
    package->state = state;
    tf_dict_resolution_changed(ctx);
    return index;
}

size_t tf_package_begin(tf_ctx *ctx, const char *name, size_t name_len,
                        const char *path) {
    return package_add(ctx, name, name_len, path, TF_PACKAGE_LOADING);
}

size_t tf_package_add_registered(tf_ctx *ctx, const char *name,
                                 size_t name_len, const char *identity) {
    return package_add(ctx, name, name_len, identity, TF_PACKAGE_LOADED);
}

void tf_package_finish(tf_ctx *ctx, size_t package_index, tf_ret status) {
    if (!ctx || package_index == TF_ROOT_PACKAGE ||
        package_index >= ctx->packages.len) {
        return;
    }
    tf_package_state state =
        status == TF_OK ? TF_PACKAGE_LOADED : TF_PACKAGE_FAILED;
    if (ctx->packages.entries[package_index].state != state) {
        ctx->packages.entries[package_index].state = state;
        tf_dict_resolution_changed(ctx);
    }
}

const tf_package *tf_package_get(tf_ctx *ctx, size_t package_index) {
    if (!ctx || package_index >= ctx->packages.len) return NULL;
    return &ctx->packages.entries[package_index];
}

size_t tf_package_import_find(tf_ctx *ctx, size_t owner_package_index,
                              const char *name, size_t name_len) {
    if (!ctx || !name) return (size_t)-1;
    for (size_t i = 0; i < ctx->package_imports.len; i++) {
        tf_package_import *imported = &ctx->package_imports.entries[i];
        if (imported->owner_package_index == owner_package_index &&
            imported->name_len == name_len &&
            memcmp(imported->name, name, name_len) == 0) {
            return imported->target_package_index;
        }
    }
    return (size_t)-1;
}

bool tf_package_import_add(tf_ctx *ctx, size_t owner_package_index,
                           const char *name, size_t name_len,
                           size_t target_package_index) {
    size_t existing = tf_package_import_find(ctx, owner_package_index, name,
                                             name_len);
    if (existing != (size_t)-1) return existing == target_package_index;

    if (ctx->package_imports.len >= ctx->package_imports.cap) {
        ctx->package_imports.cap *= 2;
        ctx->package_imports.entries =
            tf_xrealloc(ctx->package_imports.entries,
                        sizeof(tf_package_import) * ctx->package_imports.cap);
    }

    tf_package_import *imported =
        &ctx->package_imports.entries[ctx->package_imports.len++];
    imported->name = tf_xmalloc(name_len + 1);
    memcpy(imported->name, name, name_len);
    imported->name[name_len] = '\0';
    imported->name_len = name_len;
    imported->owner_package_index = owner_package_index;
    imported->target_package_index = target_package_index;
    tf_dict_resolution_changed(ctx);
    return true;
}

void tf_package_import_remove(tf_ctx *ctx, size_t owner_package_index,
                              const char *name, size_t name_len,
                              size_t target_package_index) {
    if (!ctx || !name) return;
    for (size_t i = 0; i < ctx->package_imports.len; i++) {
        tf_package_import *imported = &ctx->package_imports.entries[i];
        if (imported->owner_package_index != owner_package_index ||
            imported->target_package_index != target_package_index ||
            imported->name_len != name_len ||
            memcmp(imported->name, name, name_len) != 0) {
            continue;
        }
        free(imported->name);
        ctx->package_imports.len--;
        if (i != ctx->package_imports.len) {
            ctx->package_imports.entries[i] =
                ctx->package_imports.entries[ctx->package_imports.len];
        }
        tf_dict_resolution_changed(ctx);
        return;
    }
}

void tf_ctx_set_core_package_path(tf_ctx *ctx, const char *path) {
    if (!ctx) return;
    free(ctx->core_package_path);
    ctx->core_package_path = path ? tf_xstrdup(path) : NULL;
}

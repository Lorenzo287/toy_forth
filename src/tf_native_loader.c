#include "tf_native_loader.h"

#include "tf_alloc.h"
#include "toy.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void *library_open(const char *path) {
    /* Package paths are absolute and normalized to '/', but the altered DLL
       search order needs a native path to use the extension directory. */
    char *native_path = tf_xstrdup(path);
    for (char *cursor = native_path; *cursor; cursor++) {
        if (*cursor == '/') *cursor = '\\';
    }
    HMODULE handle =
        LoadLibraryExA(native_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    free(native_path);
    return (void *)handle;
}

static toy_extension_entry library_entry(void *handle) {
    FARPROC symbol = GetProcAddress((HMODULE)handle, TOY_EXTENSION_ENTRY_SYMBOL);
    toy_extension_entry entry = NULL;
    _Static_assert(sizeof(entry) == sizeof(symbol),
                   "function pointer size mismatch");
    memcpy(&entry, &symbol, sizeof(entry));
    return entry;
}

static const char *library_error(void) {
    static char message[512];
    DWORD code = GetLastError();
    DWORD length = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code,
        0, message, (DWORD)sizeof(message), NULL);
    while (length > 0 &&
           (message[length - 1] == '\r' || message[length - 1] == '\n')) {
        message[--length] = '\0';
    }
    return length > 0 ? message : "unknown platform error";
}

static void library_close(void *handle) {
    FreeLibrary((HMODULE)handle);
}
#else
#include <dlfcn.h>
static void *library_open(const char *path) {
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

static toy_extension_entry library_entry(void *handle) {
    dlerror();
    void *symbol = dlsym(handle, TOY_EXTENSION_ENTRY_SYMBOL);
    toy_extension_entry entry = NULL;
    _Static_assert(sizeof(entry) == sizeof(symbol),
                   "function pointer size mismatch");
    memcpy(&entry, &symbol, sizeof(entry));
    return entry;
}

static const char *library_error(void) {
    const char *message = dlerror();
    return message ? message : "unknown platform error";
}

static void library_close(void *handle) {
    dlclose(handle);
}
#endif

static const toy_extension_api extension_api = {
    .struct_size = sizeof(toy_extension_api),
    .stack_size = toy_stack_size,
    .stack_type = toy_stack_type,
    .get_bool = toy_get_bool,
    .get_int = toy_get_int,
    .get_float = toy_get_float,
    .get_string = toy_get_string,
    .get_resource = toy_get_resource,
    .get_resource_type = toy_get_resource_type,
    .pop = toy_pop,
    .push_bool = toy_push_bool,
    .push_int = toy_push_int,
    .push_float = toy_push_float,
    .push_string = toy_push_string,
    .push_resource = toy_push_resource,
    .get_error = toy_get_error,
    .clear_error = toy_clear_error,
    .fail = toy_fail,
    .interrupt = toy_interrupt,
    .random_seed = toy_random_seed,
    .random_int = toy_random_int,
    .value_retain = toy_value_retain,
    .value_release = toy_value_release,
    .value_type = toy_value_type,
    .value_get_bool = toy_value_get_bool,
    .value_get_int = toy_value_get_int,
    .value_get_float = toy_value_get_float,
    .value_get_string = toy_value_get_string,
    .value_get_resource = toy_value_get_resource,
    .value_get_resource_type = toy_value_get_resource_type,
    .push_value = toy_push_value,
    .sequence_size = toy_sequence_size,
    .sequence_get = toy_sequence_get,
    .map_size = toy_map_size,
    .map_entry = toy_map_entry,
    .make_vector = toy_make_vector,
    .make_map = toy_make_map,
    .call_value = toy_call_value,
};

static void remember_library(tf_ctx *ctx, void *handle) {
    tf_native_library_table *libraries = &ctx->native_libraries;
    if (libraries->len >= libraries->cap) {
        libraries->cap = libraries->cap ? libraries->cap * 2 : 4;
        libraries->handles = tf_xrealloc(
            libraries->handles, sizeof(void *) * libraries->cap);
    }
    libraries->handles[libraries->len++] = handle;
}

tf_ret tf_native_package_load(tf_ctx *ctx, size_t package_index,
                              const char *path) {
    void *handle = library_open(path);
    if (!handle) {
        tf_ctx_runtime_errorf(
            ctx, "failed to open C extension '%s' or one of its "
                 "dependencies: %s\n",
            path, library_error());
        return TF_ERR;
    }

    toy_extension_entry entry = library_entry(handle);
    if (!entry) {
        tf_ctx_runtime_errorf(
            ctx, "C extension '%s' does not export %s: %s\n", path,
            TOY_EXTENSION_ENTRY_SYMBOL, library_error());
        library_close(handle);
        return TF_ERR;
    }

    const toy_extension *extension =
        entry(TOY_EXTENSION_ABI_VERSION, &extension_api);
    if (!extension) {
        tf_ctx_runtime_errorf(ctx,
                              "C extension '%s' rejected the host ABI\n",
                              path);
        library_close(handle);
        return TF_ERR;
    }
    if (extension->struct_size != sizeof(toy_extension)) {
        tf_ctx_runtime_errorf(ctx,
                              "C extension '%s' has an incompatible "
                              "descriptor\n",
                              path);
        library_close(handle);
        return TF_ERR;
    }

    toy_native_package package = {
        extension->name,
        extension->words,
        extension->word_count,
    };
    if (tf_install_native_package(ctx, package_index, &package) != TOY_OK) {
        library_close(handle);
        return TF_ERR;
    }

    remember_library(ctx, handle);
    return TF_OK;
}

void tf_native_packages_close(tf_ctx *ctx) {
    if (!ctx) return;
    tf_native_library_table *libraries = &ctx->native_libraries;
    while (libraries->len > 0) {
        library_close(libraries->handles[--libraries->len]);
    }
    free(libraries->handles);
    libraries->handles = NULL;
    libraries->cap = 0;
}

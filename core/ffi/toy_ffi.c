#define TOY_EXTENSION_IMPLEMENTATION
#include "toy.h"

#include <ffi.h>

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#define TOY_FFI_LIBRARY_TYPE "ffi.library"
#define TOY_FFI_FUNCTION_TYPE "ffi.function"
#define TOY_FFI_MAX_ARGUMENTS 32

typedef enum {
    FOREIGN_VOID,
    FOREIGN_BOOL,
    FOREIGN_I8,
    FOREIGN_U8,
    FOREIGN_I16,
    FOREIGN_U16,
    FOREIGN_I32,
    FOREIGN_U32,
    FOREIGN_I64,
    FOREIGN_U64,
    FOREIGN_ISIZE,
    FOREIGN_USIZE,
    FOREIGN_F32,
    FOREIGN_F64,
    FOREIGN_CSTR,
} foreign_kind;

typedef struct {
    void *handle;
    size_t references;
} foreign_library;

typedef struct {
    ffi_cif cif;
    ffi_type **argument_types;
    foreign_kind *argument_kinds;
    size_t argument_count;
    foreign_kind return_kind;
    foreign_library *library;
    void (*code)(void);
} foreign_function;

typedef struct {
    ffi_type **argument_types;
    foreign_kind *argument_kinds;
    size_t argument_count;
    foreign_kind return_kind;
} foreign_signature;

typedef union {
    ffi_arg argument;
    ffi_sarg signed_argument;
    uint8_t u8;
    int8_t i8;
    uint16_t u16;
    int16_t i16;
    uint32_t u32;
    int32_t i32;
    uint64_t u64;
    int64_t i64;
    uintptr_t usize;
    intptr_t isize;
    float f32;
    double f64;
    char *cstr;
} foreign_slot;

static toy_status failf(toy_state *state, const char *format, ...) {
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    return toy_fail(state, message);
}

static bool copy_string(toy_state *state, size_t depth, const char *purpose,
                        char **result) {
    const char *data = NULL;
    size_t length = 0;
    if (!toy_get_string(state, depth, &data, &length)) {
        failf(state, "%s expected a string", purpose);
        return false;
    }
    if (memchr(data, '\0', length)) {
        failf(state, "%s does not accept embedded NUL bytes", purpose);
        return false;
    }
    char *copy = malloc(length + 1);
    if (!copy) {
        failf(state, "%s could not allocate a C string", purpose);
        return false;
    }
    memcpy(copy, data, length);
    copy[length] = '\0';
    *result = copy;
    return true;
}

#ifdef _WIN32
static void *platform_library_open(const char *path) {
    return (void *)LoadLibraryA(path);
}

static void platform_library_close(void *handle) {
    FreeLibrary((HMODULE)handle);
}

static const char *platform_error(void) {
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

static bool platform_symbol(void *handle, const char *name,
                            void (**code)(void)) {
    FARPROC symbol = GetProcAddress((HMODULE)handle, name);
    if (!symbol) return false;
    _Static_assert(sizeof(*code) == sizeof(symbol),
                   "function pointer size mismatch");
    memcpy(code, &symbol, sizeof(*code));
    return true;
}
#else
static char symbol_error[512];

static void *platform_library_open(const char *path) {
    symbol_error[0] = '\0';
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

static void platform_library_close(void *handle) {
    dlclose(handle);
}

static const char *platform_error(void) {
    const char *message = dlerror();
    if (message) return message;
    return symbol_error[0] ? symbol_error : "unknown platform error";
}

static bool platform_symbol(void *handle, const char *name,
                            void (**code)(void)) {
    dlerror();
    void *symbol = dlsym(handle, name);
    const char *error = dlerror();
    if (error) {
        snprintf(symbol_error, sizeof(symbol_error), "%s", error);
        return false;
    }
    symbol_error[0] = '\0';
    _Static_assert(sizeof(*code) == sizeof(symbol),
                   "function pointer size mismatch");
    memcpy(code, &symbol, sizeof(*code));
    return true;
}
#endif

static void library_retain(foreign_library *library) {
    library->references++;
}

static void library_release(foreign_library *library) {
    if (--library->references > 0) return;
    platform_library_close(library->handle);
    free(library);
}

static void destroy_library(void *resource, void *userdata) {
    (void)userdata;
    library_release(resource);
}

static void destroy_function(void *resource, void *userdata) {
    (void)userdata;
    foreign_function *function = resource;
    library_release(function->library);
    free(function->argument_types);
    free(function->argument_kinds);
    free(function);
}

static const char *kind_name(foreign_kind kind) {
    switch (kind) {
        case FOREIGN_VOID: return "void";
        case FOREIGN_BOOL: return "bool";
        case FOREIGN_I8: return "i8";
        case FOREIGN_U8: return "u8";
        case FOREIGN_I16: return "i16";
        case FOREIGN_U16: return "u16";
        case FOREIGN_I32: return "i32";
        case FOREIGN_U32: return "u32";
        case FOREIGN_I64: return "i64";
        case FOREIGN_U64: return "u64";
        case FOREIGN_ISIZE: return "isize";
        case FOREIGN_USIZE: return "usize";
        case FOREIGN_F32: return "f32";
        case FOREIGN_F64: return "f64";
        case FOREIGN_CSTR: return "cstr";
    }
    return "unknown";
}

static ffi_type *kind_ffi_type(foreign_kind kind) {
    switch (kind) {
        case FOREIGN_VOID: return &ffi_type_void;
        case FOREIGN_BOOL: return &ffi_type_uint8;
        case FOREIGN_I8: return &ffi_type_sint8;
        case FOREIGN_U8: return &ffi_type_uint8;
        case FOREIGN_I16: return &ffi_type_sint16;
        case FOREIGN_U16: return &ffi_type_uint16;
        case FOREIGN_I32: return &ffi_type_sint32;
        case FOREIGN_U32: return &ffi_type_uint32;
        case FOREIGN_I64: return &ffi_type_sint64;
        case FOREIGN_U64: return &ffi_type_uint64;
        case FOREIGN_ISIZE:
            return sizeof(intptr_t) == sizeof(int64_t) ? &ffi_type_sint64
                                                       : &ffi_type_sint32;
        case FOREIGN_USIZE:
            return sizeof(uintptr_t) == sizeof(uint64_t) ? &ffi_type_uint64
                                                         : &ffi_type_uint32;
        case FOREIGN_F32: return &ffi_type_float;
        case FOREIGN_F64: return &ffi_type_double;
        case FOREIGN_CSTR: return &ffi_type_pointer;
    }
    return NULL;
}

static void skip_space(const char *text, size_t length, size_t *position) {
    while (*position < length &&
           (text[*position] == ' ' || text[*position] == '\t' ||
            text[*position] == '\r' || text[*position] == '\n')) {
        (*position)++;
    }
}

static bool token_kind(const char *token, size_t length, foreign_kind *kind) {
#define MATCH(name, value)                                                   \
    if (length == sizeof(name) - 1 && memcmp(token, name, length) == 0) {    \
        *kind = value;                                                       \
        return true;                                                         \
    }
    MATCH("void", FOREIGN_VOID)
    MATCH("bool", FOREIGN_BOOL)
    MATCH("i8", FOREIGN_I8)
    MATCH("u8", FOREIGN_U8)
    MATCH("i16", FOREIGN_I16)
    MATCH("u16", FOREIGN_U16)
    MATCH("i32", FOREIGN_I32)
    MATCH("u32", FOREIGN_U32)
    MATCH("i64", FOREIGN_I64)
    MATCH("u64", FOREIGN_U64)
    MATCH("isize", FOREIGN_ISIZE)
    MATCH("usize", FOREIGN_USIZE)
    MATCH("f32", FOREIGN_F32)
    MATCH("f64", FOREIGN_F64)
    MATCH("cstr", FOREIGN_CSTR)
#undef MATCH
    return false;
}

static bool parse_kind(const char *text, size_t length, size_t *position,
                       bool allow_void, foreign_kind *kind, char *error,
                       size_t error_size) {
    skip_space(text, length, position);
    size_t start = *position;
    while (*position < length) {
        char c = text[*position];
        if ((c < 'a' || c > 'z') && (c < '0' || c > '9')) break;
        (*position)++;
    }
    if (start == *position ||
        !token_kind(text + start, *position - start, kind)) {
        snprintf(error, error_size, "unknown type at byte %zu", start);
        return false;
    }
    if (*kind == FOREIGN_VOID && !allow_void) {
        snprintf(error, error_size, "void is only valid as a return type");
        return false;
    }
    return true;
}

static void signature_clear(foreign_signature *signature) {
    free(signature->argument_types);
    free(signature->argument_kinds);
    memset(signature, 0, sizeof(*signature));
}

static bool parse_signature(const char *text, foreign_signature *signature,
                            char *error, size_t error_size) {
    size_t length = strlen(text);
    size_t position = 0;
    foreign_kind arguments[TOY_FFI_MAX_ARGUMENTS];
    size_t argument_count = 0;

    memset(signature, 0, sizeof(*signature));
    while (true) {
        skip_space(text, length, &position);
        if (position + 1 < length && text[position] == '-' &&
            text[position + 1] == '>') {
            position += 2;
            break;
        }
        if (position >= length) {
            snprintf(error, error_size, "expected '->' after the argument types");
            return false;
        }
        if (argument_count >= TOY_FFI_MAX_ARGUMENTS) {
            snprintf(error, error_size,
                     "signatures support at most %d arguments",
                     TOY_FFI_MAX_ARGUMENTS);
            return false;
        }
        if (!parse_kind(text, length, &position, false,
                        &arguments[argument_count], error, error_size)) {
            return false;
        }
        argument_count++;
    }

    if (!parse_kind(text, length, &position, true, &signature->return_kind,
                    error, error_size)) {
        return false;
    }
    skip_space(text, length, &position);
    if (position != length) {
        snprintf(error, error_size, "unexpected text at byte %zu", position);
        return false;
    }

    if (argument_count > 0) {
        signature->argument_types =
            malloc(sizeof(ffi_type *) * argument_count);
        signature->argument_kinds =
            malloc(sizeof(foreign_kind) * argument_count);
        if (!signature->argument_types || !signature->argument_kinds) {
            signature_clear(signature);
            snprintf(error, error_size, "could not allocate the signature");
            return false;
        }
        for (size_t i = 0; i < argument_count; i++) {
            signature->argument_kinds[i] = arguments[i];
            signature->argument_types[i] = kind_ffi_type(arguments[i]);
        }
    }
    signature->argument_count = argument_count;
    return true;
}

static toy_status ffi_open(toy_state *state) {
    char *path = NULL;
    if (!copy_string(state, 0, "ffi.open", &path)) return TOY_ERROR;
    if (path[0] == '\0') {
        free(path);
        return toy_fail(state, "ffi.open expected a non-empty library path");
    }

    void *handle = platform_library_open(path);
    if (!handle) {
        toy_status status = failf(state, "ffi.open could not load '%s': %s",
                                  path, platform_error());
        free(path);
        return status;
    }
    free(path);

    foreign_library *library = malloc(sizeof(*library));
    if (!library) {
        platform_library_close(handle);
        return toy_fail(state, "ffi.open could not allocate a library handle");
    }
    library->handle = handle;
    library->references = 1;

    if (!toy_pop(state, 1)) {
        library_release(library);
        return toy_fail(state, "ffi.open failed to pop its input");
    }
    toy_status status = toy_push_resource(
        state, TOY_FFI_LIBRARY_TYPE, library, destroy_library, NULL);
    if (status != TOY_OK) library_release(library);
    return status;
}

static toy_status ffi_bind(toy_state *state) {
    foreign_library *library = NULL;
    char *symbol_name = NULL;
    char *signature_text = NULL;
    if (!toy_get_resource(state, 2, TOY_FFI_LIBRARY_TYPE,
                          (void **)&library)) {
        return toy_fail(
            state, "ffi.bind expected a library, symbol, and signature");
    }
    if (!copy_string(state, 1, "ffi.bind symbol", &symbol_name)) {
        return TOY_ERROR;
    }
    if (!copy_string(state, 0, "ffi.bind signature", &signature_text)) {
        free(symbol_name);
        return TOY_ERROR;
    }

    foreign_signature signature = {0};
    char parse_error[256];
    if (!parse_signature(signature_text, &signature, parse_error,
                         sizeof(parse_error))) {
        toy_status status =
            failf(state, "ffi.bind invalid signature '%s': %s",
                  signature_text, parse_error);
        free(signature_text);
        free(symbol_name);
        return status;
    }
    free(signature_text);

    void (*code)(void) = NULL;
    if (!platform_symbol(library->handle, symbol_name, &code)) {
        toy_status status = failf(state, "ffi.bind could not resolve '%s': %s",
                                  symbol_name, platform_error());
        signature_clear(&signature);
        free(symbol_name);
        return status;
    }
    free(symbol_name);

    foreign_function *function = calloc(1, sizeof(*function));
    if (!function) {
        signature_clear(&signature);
        return toy_fail(state, "ffi.bind could not allocate a function handle");
    }
    function->argument_types = signature.argument_types;
    function->argument_kinds = signature.argument_kinds;
    function->argument_count = signature.argument_count;
    function->return_kind = signature.return_kind;
    function->library = library;
    function->code = code;

    ffi_status prep_status = ffi_prep_cif(
        &function->cif, FFI_DEFAULT_ABI, (unsigned int)function->argument_count,
        kind_ffi_type(function->return_kind), function->argument_types);
    if (prep_status != FFI_OK) {
        free(function->argument_types);
        free(function->argument_kinds);
        free(function);
        return failf(state, "ffi.bind could not prepare the signature (%d)",
                     (int)prep_status);
    }

    library_retain(library);
    if (!toy_pop(state, 3)) {
        destroy_function(function, NULL);
        return toy_fail(state, "ffi.bind failed to pop its inputs");
    }
    toy_status status = toy_push_resource(
        state, TOY_FFI_FUNCTION_TYPE, function, destroy_function, NULL);
    if (status != TOY_OK) destroy_function(function, NULL);
    return status;
}

static bool read_number(toy_state *state, size_t depth, double *value) {
    if (toy_stack_type(state, depth) == TOY_TYPE_FLOAT) {
        return toy_get_float(state, depth, value);
    }
    int64_t integer = 0;
    if (!toy_get_int(state, depth, &integer)) return false;
    *value = (double)integer;
    return true;
}

static bool read_argument(toy_state *state, foreign_kind kind, size_t index,
                          size_t depth, foreign_slot *slot) {
    int64_t integer = 0;
    double number = 0.0;
    bool boolean = false;
    switch (kind) {
        case FOREIGN_BOOL:
            if (toy_get_bool(state, depth, &boolean)) {
                slot->u8 = boolean ? 1 : 0;
                return true;
            }
            break;
        case FOREIGN_I8:
            if (toy_get_int(state, depth, &integer) && integer >= INT8_MIN &&
                integer <= INT8_MAX) {
                slot->i8 = (int8_t)integer;
                return true;
            }
            break;
        case FOREIGN_U8:
            if (toy_get_int(state, depth, &integer) && integer >= 0 &&
                integer <= UINT8_MAX) {
                slot->u8 = (uint8_t)integer;
                return true;
            }
            break;
        case FOREIGN_I16:
            if (toy_get_int(state, depth, &integer) && integer >= INT16_MIN &&
                integer <= INT16_MAX) {
                slot->i16 = (int16_t)integer;
                return true;
            }
            break;
        case FOREIGN_U16:
            if (toy_get_int(state, depth, &integer) && integer >= 0 &&
                integer <= UINT16_MAX) {
                slot->u16 = (uint16_t)integer;
                return true;
            }
            break;
        case FOREIGN_I32:
            if (toy_get_int(state, depth, &integer) && integer >= INT32_MIN &&
                integer <= INT32_MAX) {
                slot->i32 = (int32_t)integer;
                return true;
            }
            break;
        case FOREIGN_U32:
            if (toy_get_int(state, depth, &integer) && integer >= 0 &&
                (uint64_t)integer <= UINT32_MAX) {
                slot->u32 = (uint32_t)integer;
                return true;
            }
            break;
        case FOREIGN_I64:
            if (toy_get_int(state, depth, &slot->i64)) return true;
            break;
        case FOREIGN_U64:
            if (toy_get_int(state, depth, &integer) && integer >= 0) {
                slot->u64 = (uint64_t)integer;
                return true;
            }
            break;
        case FOREIGN_ISIZE:
            if (toy_get_int(state, depth, &integer) &&
                integer >= INTPTR_MIN && integer <= INTPTR_MAX) {
                slot->isize = (intptr_t)integer;
                return true;
            }
            break;
        case FOREIGN_USIZE:
            if (toy_get_int(state, depth, &integer) && integer >= 0 &&
                (uint64_t)integer <= UINTPTR_MAX) {
                slot->usize = (uintptr_t)integer;
                return true;
            }
            break;
        case FOREIGN_F32:
            if (read_number(state, depth, &number) &&
                (!isfinite(number) ||
                 (number >= -FLT_MAX && number <= FLT_MAX))) {
                slot->f32 = (float)number;
                return true;
            }
            break;
        case FOREIGN_F64:
            if (read_number(state, depth, &slot->f64)) return true;
            break;
        case FOREIGN_CSTR: {
            const char *data = NULL;
            size_t length = 0;
            if (!toy_get_string(state, depth, &data, &length) ||
                memchr(data, '\0', length)) {
                break;
            }
            slot->cstr = malloc(length + 1);
            if (!slot->cstr) {
                failf(state, "ffi.call could not allocate argument %zu", index);
                return false;
            }
            memcpy(slot->cstr, data, length);
            slot->cstr[length] = '\0';
            return true;
        }
        case FOREIGN_VOID: break;
    }
    failf(state, "ffi.call argument %zu expected %s", index,
          kind_name(kind));
    return false;
}

static void free_argument_strings(const foreign_function *function,
                                  foreign_slot *slots, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (function->argument_kinds[i] == FOREIGN_CSTR) free(slots[i].cstr);
    }
}

static toy_status push_result(toy_state *state, foreign_kind kind,
                              foreign_slot *result, const char *string_result,
                              size_t string_length) {
    switch (kind) {
        case FOREIGN_VOID: return TOY_OK;
        case FOREIGN_BOOL:
            return toy_push_bool(state, result->argument != 0);
        case FOREIGN_I8:
            return toy_push_int(state, (int8_t)result->signed_argument);
        case FOREIGN_U8:
            return toy_push_int(state, (uint8_t)result->argument);
        case FOREIGN_I16:
            return toy_push_int(state, (int16_t)result->signed_argument);
        case FOREIGN_U16:
            return toy_push_int(state, (uint16_t)result->argument);
        case FOREIGN_I32:
            return toy_push_int(state, (int32_t)result->signed_argument);
        case FOREIGN_U32:
            return toy_push_int(state, (uint32_t)result->argument);
        case FOREIGN_I64: return toy_push_int(state, result->i64);
        case FOREIGN_U64:
            if (result->u64 > INT64_MAX) {
                return toy_fail(state,
                                "ffi.call returned a u64 outside Toy's range");
            }
            return toy_push_int(state, (int64_t)result->u64);
        case FOREIGN_ISIZE: return toy_push_int(state, result->isize);
        case FOREIGN_USIZE:
            if (result->usize > INT64_MAX) {
                return toy_fail(
                    state, "ffi.call returned a usize outside Toy's range");
            }
            return toy_push_int(state, (int64_t)result->usize);
        case FOREIGN_F32: return toy_push_float(state, result->f32);
        case FOREIGN_F64: return toy_push_float(state, result->f64);
        case FOREIGN_CSTR:
            if (!string_result) {
                return toy_fail(state, "ffi.call returned a null cstr");
            }
            return toy_push_string(state, string_result, string_length);
    }
    return toy_fail(state, "ffi.call has an invalid return type");
}

static toy_status ffi_invoke(toy_state *state) {
    foreign_function *function = NULL;
    if (!toy_get_resource(state, 0, TOY_FFI_FUNCTION_TYPE,
                          (void **)&function)) {
        return toy_fail(state, "ffi.call expected a bound function on top");
    }
    if (toy_stack_size(state) < function->argument_count + 1) {
        return failf(state, "ffi.call expected %zu arguments below the function",
                     function->argument_count);
    }

    foreign_slot *slots = NULL;
    void **argument_values = NULL;
    if (function->argument_count > 0) {
        slots = calloc(function->argument_count, sizeof(*slots));
        argument_values =
            malloc(function->argument_count * sizeof(*argument_values));
        if (!slots || !argument_values) {
            free(slots);
            free(argument_values);
            return toy_fail(state, "ffi.call could not allocate argument storage");
        }
    }

    size_t converted = 0;
    for (size_t i = 0; i < function->argument_count; i++) {
        size_t depth = function->argument_count - i;
        if (!read_argument(state, function->argument_kinds[i], i, depth,
                           &slots[i])) {
            free_argument_strings(function, slots, converted);
            free(argument_values);
            free(slots);
            return TOY_ERROR;
        }
        argument_values[i] = &slots[i];
        converted++;
    }

    foreign_kind return_kind = function->return_kind;
    size_t input_count = function->argument_count + 1;
    foreign_slot result = {0};
    ffi_call(&function->cif, FFI_FN(function->code),
             return_kind == FOREIGN_VOID ? NULL : &result, argument_values);
    free_argument_strings(function, slots, converted);
    free(argument_values);
    free(slots);

    char *string_result = NULL;
    size_t string_length = 0;
    if (return_kind == FOREIGN_CSTR && result.cstr) {
        string_length = strlen(result.cstr);
        string_result = malloc(string_length + 1);
        if (string_result) memcpy(string_result, result.cstr, string_length + 1);
    }

    if (!toy_pop(state, input_count)) {
        free(string_result);
        return toy_fail(state, "ffi.call failed to pop its inputs");
    }
    if (return_kind == FOREIGN_CSTR && result.cstr && !string_result) {
        return toy_fail(state, "ffi.call could not copy its cstr result");
    }
    toy_status status = push_result(state, return_kind, &result, string_result,
                                    string_length);
    free(string_result);
    return status;
}

#include "generated_words.inc"

static const toy_extension ffi_extension = {
    sizeof(toy_extension),
    "ffi",
    ffi_words,
    sizeof(ffi_words) / sizeof(ffi_words[0]),
};

TOY_EXTENSION_EXPORT const toy_extension *toy_extension_init(
    uint32_t abi_version, const toy_extension_api *api) {
    if (!toy_extension_bind(abi_version, api)) return NULL;
    return &ffi_extension;
}

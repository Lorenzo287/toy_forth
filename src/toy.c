#include "toy.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tf_alloc.h"
#include "tf_exec.h"
#include "tf_builtins.h"
#include "tf_obj.h"
#include "tf_parser.h"

struct toy_value {
    toy_state *state;
    tf_obj *object;
};

static bool state_is_idle(toy_state *state) {
    if (!state) return false;
    if (state->call_stack_len == 0) return true;
    tf_ctx_set_error(state, "the Toy state is already executing");
    return false;
}

static toy_status api_errorf(toy_state *state, const char *format, ...) {
    if (!state) return TOY_ERROR;

    va_list args;
    va_start(args, format);
    va_list count_args;
    va_copy(count_args, args);
    int length = vsnprintf(NULL, 0, format, count_args);
    va_end(count_args);
    if (length < 0) {
        va_end(args);
        tf_ctx_set_error(state, "package registration failed");
        return TOY_ERROR;
    }

    char *message = tf_xmalloc((size_t)length + 1);
    vsnprintf(message, (size_t)length + 1, format, args);
    va_end(args);
    tf_ctx_set_error(state, message);
    free(message);
    return TOY_ERROR;
}

static toy_type value_type(tf_obj *value) {
    if (!value) return TOY_TYPE_MISSING;

    switch (tf_obj_typeof(value)) {
    case TF_OBJ_TYPE_BOOL:
        return TOY_TYPE_BOOL;
    case TF_OBJ_TYPE_INT:
        return TOY_TYPE_INT;
    case TF_OBJ_TYPE_FLOAT:
        return TOY_TYPE_FLOAT;
    case TF_OBJ_TYPE_STR:
        return TOY_TYPE_STRING;
    case TF_OBJ_TYPE_SYMBOL:
        return TOY_TYPE_SYMBOL;
    case TF_OBJ_TYPE_CALL:
        return TOY_TYPE_CALL;
    case TF_OBJ_TYPE_VECTOR:
        return TOY_TYPE_VECTOR;
    case TF_OBJ_TYPE_LIST:
        return TOY_TYPE_LIST;
    case TF_OBJ_TYPE_MAP:
        return TOY_TYPE_MAP;
    case TF_OBJ_TYPE_SET:
        return TOY_TYPE_SET;
    case TF_OBJ_TYPE_DEQUE:
        return TOY_TYPE_DEQUE;
    case TF_OBJ_TYPE_PQUEUE:
        return TOY_TYPE_PQUEUE;
    case TF_OBJ_TYPE_RESOURCE:
        return TOY_TYPE_RESOURCE;
    case TF_OBJ_TYPE_VARLIST:
    case TF_OBJ_TYPE_VARFETCH:
        return TOY_TYPE_INTERNAL;
    }
    return TOY_TYPE_INTERNAL;
}

static toy_value *value_retain_object(toy_state *state, tf_obj *object) {
    if (!state || !object) return NULL;
    toy_value *value = tf_xmalloc(sizeof(*value));
    value->state = state;
    value->object = object;
    tf_obj_retain(object);
    return value;
}

static toy_value *value_take_object(toy_state *state, tf_obj *object) {
    if (!state || !object) return NULL;
    toy_value *value = tf_xmalloc(sizeof(*value));
    value->state = state;
    value->object = object;
    return value;
}

toy_state *toy_state_new(const toy_state_config *config) {
    toy_state *state = tf_ctx_new(0, NULL);
    if (!state || !config) return state;
    tf_ctx_set_output(state, config->output, config->output_userdata);
    tf_ctx_set_diagnostic(state, config->diagnostic,
                          config->diagnostic_userdata);
    tf_ctx_set_core_package_path(state, config->core_package_path);
    return state;
}

void toy_state_free(toy_state *state) {
    if (!state) return;
    tf_ctx_free(state);
}

toy_status toy_eval(toy_state *state, const char *source_name,
                    const char *source) {
    if (!state || !source) return TOY_ERROR;
    if (!state_is_idle(state)) return TOY_ERROR;

    tf_ctx_clear_error(state);
    tf_obj *program = tf_parse_source(state, source_name, source);
    if (!program) {
        if (!tf_ctx_last_error(state)) {
            tf_ctx_set_error(state, "source parsing failed");
        }
        return TOY_ERROR;
    }

    toy_status result = tf_vm_exec(state, program);
    tf_obj_release(program);
    state->current_span = (tf_source_span){0};
    state->current_word = NULL;
    return result;
}

toy_status toy_call(toy_state *state, const char *word) {
    if (!state || !word || word[0] == '\0') return TOY_ERROR;
    if (!state_is_idle(state)) return TOY_ERROR;

    tf_obj_pool *previous_pool = tf_obj_pool_enter(&state->objects);
    tf_obj *program = tf_obj_new_vector();
    tf_vector_push(program, tf_obj_new_call(word, strlen(word)));
    tf_ret result = tf_vm_exec(state, program);
    tf_obj_release(program);
    tf_obj_pool_leave(previous_pool);
    state->current_span = (tf_source_span){0};
    state->current_word = NULL;
    return result;
}

toy_status toy_register_word(toy_state *state, const char *name,
                             toy_native_fn function) {
    if (!state || !name || name[0] == '\0' || !function) return TOY_ERROR;
    if (!state_is_idle(state)) return TOY_ERROR;
    if (!tf_package_word_name_valid(name, strlen(name))) {
        return api_errorf(state, "invalid standalone native word name '%s'",
                          name);
    }
    tf_dict_set_native_copy(state, name, function);
    return TOY_OK;
}

static toy_status validate_native_package(toy_state *state,
                                          const toy_native_package *package) {
    if (!state) return TOY_ERROR;
    if (!package || !package->name || package->name[0] == '\0') {
        return api_errorf(state, "package descriptor is invalid");
    }

    size_t package_name_len = strlen(package->name);
    if (!tf_package_name_valid(package->name, package_name_len)) {
        return api_errorf(state, "invalid package name '%s'",
                          package->name);
    }
    if (!package->words || package->word_count == 0) {
        return api_errorf(state, "package '%s' has no words",
                          package->name);
    }
    for (size_t i = 0; i < package->word_count; i++) {
        const toy_native_word *word = &package->words[i];
        if (!word->name || !word->callback) {
            return api_errorf(state,
                              "package '%s' has an invalid word descriptor",
                              package->name);
        }

        size_t word_name_len = strlen(word->name);
        if (!tf_package_word_name_valid(word->name, word_name_len)) {
            return api_errorf(state,
                              "invalid native word name '%s' in package '%s'",
                              word->name, package->name);
        }
        for (size_t j = 0; j < i; j++) {
            if (strcmp(package->words[j].name, word->name) == 0) {
                return api_errorf(state,
                                  "package word '%s.%s' is duplicated",
                                  package->name, word->name);
            }
        }
    }
    return TOY_OK;
}

toy_status tf_install_native_package(toy_state *state, size_t package_index,
                                     const toy_native_package *package) {
    toy_status validation = validate_native_package(state, package);
    if (validation != TOY_OK) return validation;

    size_t package_name_len = strlen(package->name);
    const tf_package *registered = tf_package_get(state, package_index);
    if (!registered || registered->name_len != package_name_len ||
        memcmp(registered->name, package->name, package_name_len) != 0) {
        return api_errorf(state,
                          "C extension exports package '%s', expected '%s'",
                          package->name,
                          registered ? registered->name : "<missing>");
    }

    for (size_t i = 0; i < package->word_count; i++) {
        const toy_native_word *word = &package->words[i];
        if (tf_dict_lookup_scoped(state, package_index, word->name,
                                  strlen(word->name))) {
            return api_errorf(state, "word '%s.%s' is already defined",
                              package->name, word->name);
        }
    }

    for (size_t i = 0; i < package->word_count; i++) {
        tf_dict_add_native_scoped(state, package->words[i].name,
                                  strlen(package->words[i].name),
                                  package_index,
                                  package->words[i].callback,
                                  package->words[i].stack_effect,
                                  package->words[i].description);
    }
    return TOY_OK;
}

toy_status toy_register_package(toy_state *state,
                                const toy_native_package *package) {
    if (!state || !state_is_idle(state)) return TOY_ERROR;
    toy_status validation = validate_native_package(state, package);
    if (validation != TOY_OK) return validation;
    if (tf_package_import_find(state, TF_ROOT_PACKAGE, package->name,
                               strlen(package->name)) != (size_t)-1) {
        return api_errorf(state, "package '%s' is already registered",
                          package->name);
    }
    char *identity = tf_xmalloc(strlen(package->name) + 6);
    sprintf(identity, "host:%s", package->name);
    if (tf_package_find_path(state, identity) != (size_t)-1) {
        free(identity);
        return api_errorf(state, "package '%s' is already registered",
                          package->name);
    }
    size_t index = tf_package_add_registered(state, package->name,
                                             strlen(package->name), identity);
    free(identity);
    toy_status status = tf_install_native_package(state, index, package);
    if (status != TOY_OK) {
        tf_package_finish(state, index, TF_ERR);
        return status;
    }
    if (!tf_package_import_add(state, TF_ROOT_PACKAGE, package->name,
                               strlen(package->name), index)) {
        tf_package_finish(state, index, TF_ERR);
        return api_errorf(state, "failed to import package '%s'",
                          package->name);
    }
    return TOY_OK;
}

toy_status toy_import_package(toy_state *state, const char *path,
                              const char *alias) {
    if (!state || !path || !state_is_idle(state)) return TOY_ERROR;
    tf_ctx_clear_error(state);
    state->current_span = (tf_source_span){0};
    state->current_word = NULL;
    toy_status result = tf_package_load(state, path, TF_ROOT_PACKAGE, alias,
                                        alias ? strlen(alias) : 0, NULL);
    state->current_span = (tf_source_span){0};
    state->current_word = NULL;
    return result;
}

toy_status toy_run_package(toy_state *state, const char *path) {
    if (!state || !path || !state_is_idle(state)) return TOY_ERROR;
    tf_ctx_clear_error(state);
    state->current_span = (tf_source_span){0};
    state->current_word = NULL;
    toy_status result = tf_package_run_main(state, path);
    state->current_span = (tf_source_span){0};
    state->current_word = NULL;
    return result;
}

size_t toy_stack_size(toy_state *state) {
    return state ? tf_stack_len(state) : 0;
}

toy_type toy_stack_type(toy_state *state, size_t depth) {
    tf_obj *value = state ? tf_stack_peek(state, depth) : NULL;
    return value_type(value);
}

bool toy_get_bool(toy_state *state, size_t depth, bool *value) {
    tf_obj *object = state ? tf_stack_peek(state, depth) : NULL;
    if (!object || tf_obj_typeof(object) != TF_OBJ_TYPE_BOOL || !value) {
        return false;
    }
    *value = object->b;
    return true;
}

bool toy_get_int(toy_state *state, size_t depth, int64_t *value) {
    tf_obj *object = state ? tf_stack_peek(state, depth) : NULL;
    if (!object || tf_obj_typeof(object) != TF_OBJ_TYPE_INT || !value) {
        return false;
    }
    *value = tf_obj_int_value(object);
    return true;
}

bool toy_get_float(toy_state *state, size_t depth, double *value) {
    tf_obj *object = state ? tf_stack_peek(state, depth) : NULL;
    if (!object || tf_obj_typeof(object) != TF_OBJ_TYPE_FLOAT || !value) {
        return false;
    }
    *value = object->f;
    return true;
}

bool toy_get_string(toy_state *state, size_t depth, const char **data,
                    size_t *length) {
    tf_obj *object = state ? tf_stack_peek(state, depth) : NULL;
    if (!object || tf_obj_typeof(object) != TF_OBJ_TYPE_STR || !data ||
        !length) {
        return false;
    }
    *data = object->str.ptr;
    *length = object->str.len;
    return true;
}

bool toy_get_resource(toy_state *state, size_t depth,
                      const char *expected_type, void **resource) {
    tf_obj *object = state ? tf_stack_peek(state, depth) : NULL;
    if (!object || tf_obj_typeof(object) != TF_OBJ_TYPE_RESOURCE ||
        !expected_type || expected_type[0] == '\0' || !resource) {
        return false;
    }
    size_t expected_len = strlen(expected_type);
    if (object->resource.type_len != expected_len ||
        memcmp(object->resource.type_name, expected_type, expected_len) != 0) {
        return false;
    }
    *resource = object->resource.pointer;
    return true;
}

bool toy_get_resource_type(toy_state *state, size_t depth,
                           const char **type_name) {
    tf_obj *object = state ? tf_stack_peek(state, depth) : NULL;
    if (!object || tf_obj_typeof(object) != TF_OBJ_TYPE_RESOURCE ||
        !type_name) {
        return false;
    }
    *type_name = object->resource.type_name;
    return true;
}

bool toy_pop(toy_state *state, size_t count) {
    if (!state || tf_stack_len(state) < count) return false;
    for (size_t i = 0; i < count; i++) {
        tf_obj_release(tf_stack_pop(state));
    }
    return true;
}

toy_status toy_push_bool(toy_state *state, bool value) {
    if (!state) return TOY_ERROR;
    tf_obj_pool *previous_pool = tf_obj_pool_enter(&state->objects);
    tf_stack_push(state, tf_obj_new_bool(value));
    tf_obj_pool_leave(previous_pool);
    return TOY_OK;
}

toy_status toy_push_int(toy_state *state, int64_t value) {
    if (!state) return TOY_ERROR;
    tf_obj_pool *previous_pool = tf_obj_pool_enter(&state->objects);
    tf_stack_push(state, tf_obj_new_int(value));
    tf_obj_pool_leave(previous_pool);
    return TOY_OK;
}

toy_status toy_push_float(toy_state *state, double value) {
    if (!state) return TOY_ERROR;
    tf_obj_pool *previous_pool = tf_obj_pool_enter(&state->objects);
    tf_stack_push(state, tf_obj_new_float(value));
    tf_obj_pool_leave(previous_pool);
    return TOY_OK;
}

toy_status toy_push_string(toy_state *state, const char *data, size_t length) {
    if (!state || (!data && length > 0)) return TOY_ERROR;
    tf_obj_pool *previous_pool = tf_obj_pool_enter(&state->objects);
    tf_stack_push(state, tf_obj_new_string(data ? data : "", length));
    tf_obj_pool_leave(previous_pool);
    return TOY_OK;
}

toy_status toy_push_resource(toy_state *state, const char *type_name,
                             void *resource,
                             toy_resource_destructor destructor,
                             void *destructor_userdata) {
    if (!state) return TOY_ERROR;
    if (!type_name || type_name[0] == '\0') {
        return api_errorf(state, "resource type name must not be empty");
    }
    size_t type_len = strlen(type_name);
    bool segment_start = true;
    bool valid_type = true;
    for (size_t i = 0; i < type_len; i++) {
        unsigned char c = (unsigned char)type_name[i];
        if (c == '.') {
            if (segment_start) valid_type = false;
            segment_start = true;
        } else if (segment_start) {
            if (!(isalpha(c) || c == '_')) valid_type = false;
            segment_start = false;
        } else if (!(isalnum(c) || c == '_' || c == '-')) {
            valid_type = false;
        }
    }
    if (segment_start) valid_type = false;
    if (!valid_type) {
        return api_errorf(state, "resource type name '%s' is invalid",
                          type_name);
    }
    if (!resource) {
        return api_errorf(state, "resource pointer must not be NULL");
    }
    tf_obj_pool *previous_pool = tf_obj_pool_enter(&state->objects);
    tf_stack_push(state, tf_obj_new_resource(
                             type_name, type_len, resource, destructor,
                             destructor_userdata));
    tf_obj_pool_leave(previous_pool);
    return TOY_OK;
}

toy_value *toy_value_retain(toy_state *state, size_t depth) {
    return value_retain_object(state,
                               state ? tf_stack_peek(state, depth) : NULL);
}

void toy_value_release(toy_value *value) {
    if (!value) return;
    tf_obj_release(value->object);
    free(value);
}

toy_type toy_value_type(const toy_value *value) {
    return value ? value_type(value->object) : TOY_TYPE_MISSING;
}

bool toy_value_get_bool(const toy_value *value, bool *result) {
    if (!value || tf_obj_typeof(value->object) != TF_OBJ_TYPE_BOOL ||
        !result) {
        return false;
    }
    *result = value->object->b;
    return true;
}

bool toy_value_get_int(const toy_value *value, int64_t *result) {
    if (!value || tf_obj_typeof(value->object) != TF_OBJ_TYPE_INT ||
        !result) {
        return false;
    }
    *result = tf_obj_int_value(value->object);
    return true;
}

bool toy_value_get_float(const toy_value *value, double *result) {
    if (!value || tf_obj_typeof(value->object) != TF_OBJ_TYPE_FLOAT ||
        !result) {
        return false;
    }
    *result = value->object->f;
    return true;
}

bool toy_value_get_string(const toy_value *value, const char **data,
                          size_t *length) {
    if (!value || tf_obj_typeof(value->object) != TF_OBJ_TYPE_STR || !data ||
        !length) {
        return false;
    }
    *data = value->object->str.ptr;
    *length = value->object->str.len;
    return true;
}

bool toy_value_get_resource(const toy_value *value,
                            const char *expected_type, void **resource) {
    if (!value || tf_obj_typeof(value->object) != TF_OBJ_TYPE_RESOURCE ||
        !expected_type || expected_type[0] == '\0' || !resource) {
        return false;
    }
    size_t expected_len = strlen(expected_type);
    if (value->object->resource.type_len != expected_len ||
        memcmp(value->object->resource.type_name, expected_type,
               expected_len) != 0) {
        return false;
    }
    *resource = value->object->resource.pointer;
    return true;
}

bool toy_value_get_resource_type(const toy_value *value,
                                 const char **type_name) {
    if (!value || tf_obj_typeof(value->object) != TF_OBJ_TYPE_RESOURCE ||
        !type_name) {
        return false;
    }
    *type_name = value->object->resource.type_name;
    return true;
}

toy_status toy_push_value(toy_state *state, const toy_value *value) {
    if (!state || !value) return TOY_ERROR;
    if (value->state != state) {
        return api_errorf(state, "cannot use a value retained by another state");
    }
    tf_obj_retain(value->object);
    tf_stack_push(state, value->object);
    return TOY_OK;
}

bool toy_sequence_size(const toy_value *sequence, size_t *size) {
    if (!sequence || !size) return false;
    switch (tf_obj_typeof(sequence->object)) {
    case TF_OBJ_TYPE_VECTOR:
        *size = sequence->object->vector.len;
        return true;
    case TF_OBJ_TYPE_LIST:
        *size = sequence->object->list.len;
        return true;
    case TF_OBJ_TYPE_STR:
        *size = sequence->object->str.len;
        return true;
    default:
        return false;
    }
}

toy_value *toy_sequence_get(const toy_value *sequence, size_t index) {
    if (!sequence) return NULL;
    tf_obj *object = sequence->object;
    switch (tf_obj_typeof(object)) {
    case TF_OBJ_TYPE_VECTOR:
        if (index >= object->vector.len) return NULL;
        return value_retain_object(sequence->state, object->vector.elem[index]);
    case TF_OBJ_TYPE_LIST:
        return value_retain_object(sequence->state,
                                   tf_list_get(object, index));
    case TF_OBJ_TYPE_STR:
        if (index >= object->str.len) return NULL;
        tf_obj_pool *previous_pool =
            tf_obj_pool_enter(&sequence->state->objects);
        tf_obj *item = tf_obj_new_string(object->str.ptr + index, 1);
        tf_obj_pool_leave(previous_pool);
        return value_take_object(sequence->state, item);
    default:
        return NULL;
    }
}

bool toy_map_size(const toy_value *map, size_t *size) {
    if (!map || tf_obj_typeof(map->object) != TF_OBJ_TYPE_MAP || !size) {
        return false;
    }
    *size = map->object->map.len;
    return true;
}

bool toy_map_entry(const toy_value *map, size_t index, toy_value **key,
                   toy_value **value) {
    if (!map || tf_obj_typeof(map->object) != TF_OBJ_TYPE_MAP ||
        index >= map->object->map.len || !key || !value || key == value) {
        return false;
    }
    tf_map_entry *entry = &map->object->map.entries[index];
    *key = value_retain_object(map->state, entry->key);
    *value = value_retain_object(map->state, entry->value);
    return true;
}

toy_status toy_make_vector(toy_state *state, size_t item_count) {
    if (!state) return TOY_ERROR;
    if (tf_stack_len(state) < item_count) {
        return api_errorf(state,
                          "cannot make a vector from %zu stack values",
                          item_count);
    }

    tf_obj_pool *previous_pool = tf_obj_pool_enter(&state->objects);
    tf_obj *vector = tf_obj_new_vector_with_capacity(item_count);
    for (size_t i = 0; i < item_count; i++) {
        tf_obj *item = tf_stack_peek(state, item_count - i - 1);
        tf_obj_retain(item);
        tf_vector_push(vector, item);
    }
    toy_pop(state, item_count);
    tf_stack_push(state, vector);
    tf_obj_pool_leave(previous_pool);
    return TOY_OK;
}

toy_status toy_make_map(toy_state *state, size_t pair_count) {
    if (!state) return TOY_ERROR;
    if (pair_count > SIZE_MAX / 2) {
        return api_errorf(state, "map pair count is too large");
    }
    size_t item_count = pair_count * 2;
    if (tf_stack_len(state) < item_count) {
        return api_errorf(state, "cannot make a map from %zu stack pairs",
                          pair_count);
    }

    for (size_t i = 0; i < pair_count; i++) {
        size_t key_depth = item_count - (i * 2) - 1;
        tf_obj *key = tf_stack_peek(state, key_depth);
        if (!tf_obj_hashable(key)) {
            return api_errorf(state, "map key %zu is not hashable", i);
        }
    }

    tf_obj_pool *previous_pool = tf_obj_pool_enter(&state->objects);
    tf_obj *map = tf_obj_new_map();
    tf_map_reserve(map, pair_count);
    for (size_t i = 0; i < pair_count; i++) {
        size_t key_depth = item_count - (i * 2) - 1;
        tf_obj *key = tf_stack_peek(state, key_depth);
        tf_obj *item = tf_stack_peek(state, key_depth - 1);
        (void)tf_map_set(map, key, item);
    }
    toy_pop(state, item_count);
    tf_stack_push(state, map);
    tf_obj_pool_leave(previous_pool);
    return TOY_OK;
}

toy_status toy_call_value(toy_state *state, const toy_value *callable) {
    if (!state || !callable) return TOY_ERROR;
    if (!state_is_idle(state)) return TOY_ERROR;
    if (callable->state != state) {
        return api_errorf(state, "cannot call a value retained by another state");
    }
    if (!tf_obj_is_callable(callable->object)) {
        return api_errorf(state, "retained value is not callable");
    }

    tf_obj_retain(callable->object);
    tf_stack_push(state, callable->object);
    return toy_call(state, "exec");
}

const char *toy_get_error(toy_state *state) {
    return tf_ctx_last_error(state);
}

void toy_clear_error(toy_state *state) {
    tf_ctx_clear_error(state);
}

toy_status toy_fail(toy_state *state, const char *message) {
    if (!state) return TOY_ERROR;
    tf_ctx_runtime_errorf(state, "%s\n", message ? message : "native error");
    return TOY_ERROR;
}

void toy_interrupt(toy_state *state) {
    tf_ctx_interrupt(state);
}

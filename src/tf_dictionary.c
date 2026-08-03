#include "tf_exec.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tf_alloc.h"

_Static_assert((TF_WORD_LOOKUP_CACHE_CAP &
                (TF_WORD_LOOKUP_CACHE_CAP - 1)) == 0,
               "word lookup cache capacity must be a power of two");

static unsigned long dict_hash(size_t package_index, const char *name,
                               size_t len) {
    unsigned long hash = 5381;
    for (size_t i = 0; i < sizeof(package_index); i++) {
        hash = ((hash << 5) + hash) +
               (unsigned char)(package_index >> (i * 8));
    }
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)name[i];
    }
    return hash;
}

void tf_dict_lookup_cache_clear(tf_ctx *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < TF_WORD_LOOKUP_CACHE_CAP; i++) {
        if (ctx->words.lookup_cache[i].key) {
            tf_obj_release(ctx->words.lookup_cache[i].key);
        }
    }
    memset(ctx->words.lookup_cache, 0, sizeof(ctx->words.lookup_cache));
}

void tf_dict_resolution_changed(tf_ctx *ctx) {
    if (!ctx) return;
    ctx->words.resolution_generation++;
    if (ctx->words.resolution_generation == 0) {
        tf_quick_program_cache_clear(ctx);
        tf_dict_lookup_cache_clear(ctx);
        ctx->words.resolution_generation = 1;
    }
}

static void dict_resize(tf_ctx *ctx) {
    size_t *old_buckets = ctx->words.buckets;
    ctx->words.capacity *= 2;
    ctx->words.buckets = tf_xcalloc(ctx->words.capacity, sizeof(size_t));

    for (size_t i = 0; i < ctx->words.count; i++) {
        tf_word *word = &ctx->words.entries[i];
        unsigned long hash = dict_hash(word->package_index, word->name,
                                       word->name_len);
        size_t slot = hash % ctx->words.capacity;
        while (ctx->words.buckets[slot]) {
            slot = (slot + 1) % ctx->words.capacity;
        }
        ctx->words.buckets[slot] = i + 1;
    }
    free(old_buckets);
}

tf_word *tf_dict_lookup_scoped(tf_ctx *ctx, size_t package_index,
                               const char *name, size_t len) {
    if (!ctx || !name || ctx->words.capacity == 0) return NULL;
    unsigned long hash = dict_hash(package_index, name, len);
    size_t slot = hash % ctx->words.capacity;
    while (ctx->words.buckets[slot]) {
        tf_word *word = &ctx->words.entries[ctx->words.buckets[slot] - 1];
        if (word->package_index == package_index && word->name_len == len &&
            memcmp(word->name, name, len) == 0) {
            return word;
        }
        slot = (slot + 1) % ctx->words.capacity;
    }
    return NULL;
}

static tf_word *dict_insert_word(tf_ctx *ctx, size_t package_index,
                                 const char *name, size_t name_len,
                                 bool copy_name, bool is_public) {
    if (ctx->words.count >= ctx->words.capacity * 7 / 10) dict_resize(ctx);

    unsigned long hash = dict_hash(package_index, name, name_len);
    size_t slot = hash % ctx->words.capacity;
    while (ctx->words.buckets[slot]) {
        slot = (slot + 1) % ctx->words.capacity;
    }

    if (ctx->words.count >= ctx->words.entry_capacity) {
        ctx->words.entry_capacity *= 2;
        ctx->words.entries =
            tf_xrealloc(ctx->words.entries,
                        sizeof(tf_word) * ctx->words.entry_capacity);
    }

    size_t entry_index = ctx->words.count++;
    tf_word *word = &ctx->words.entries[entry_index];
    if (copy_name) {
        char *owned_name = tf_xmalloc(name_len + 1);
        memcpy(owned_name, name, name_len);
        owned_name[name_len] = '\0';
        word->name = owned_name;
    } else {
        word->name = name;
    }
    word->name_len = name_len;
    word->owns_name = copy_name;
    word->package_index = package_index;
    word->is_public = is_public;
    word->doc_stack_effect = NULL;
    word->doc_description = NULL;
    word->type = TF_WORD_NATIVE;
    word->native_impl = NULL;
    ctx->words.buckets[slot] = entry_index + 1;
    tf_dict_resolution_changed(ctx);
    return word;
}

static void dict_clear_doc(tf_word *word) {
    free(word->doc_stack_effect);
    free(word->doc_description);
    word->doc_stack_effect = NULL;
    word->doc_description = NULL;
}

static void dict_set_doc(tf_word *word, const char *stack_effect,
                         const char *description) {
    dict_clear_doc(word);
    if (stack_effect && stack_effect[0] != '\0') {
        word->doc_stack_effect = tf_xstrdup(stack_effect);
    }
    if (description && description[0] != '\0') {
        word->doc_description = tf_xstrdup(description);
    }
}

bool tf_dict_set_doc_scoped(tf_ctx *ctx, size_t package_index,
                            const char *name, size_t name_len,
                            const char *stack_effect,
                            const char *description) {
    tf_word *word = tf_dict_lookup_scoped(ctx, package_index, name, name_len);
    if (!word) return false;
    dict_set_doc(word, stack_effect, description);
    return true;
}

static void dict_set_native(tf_ctx *ctx, const char *name, tf_native_fn cb,
                            bool copy_name) {
    size_t name_len = strlen(name);
    tf_word *word = tf_dict_lookup_scoped(ctx, TF_ROOT_PACKAGE, name,
                                          name_len);
    bool replacing = word != NULL;
    if (word) {
        if (word->type == TF_WORD_USER) tf_obj_release(word->user_impl);
        dict_clear_doc(word);
    } else {
        word = dict_insert_word(ctx, TF_ROOT_PACKAGE, name, name_len,
                                copy_name, true);
    }
    word->package_index = TF_ROOT_PACKAGE;
    word->is_public = true;
    word->type = TF_WORD_NATIVE;
    word->native_impl = cb;
    if (replacing) tf_dict_resolution_changed(ctx);
}

void tf_dict_set_native(tf_ctx *ctx, const char *name, tf_native_fn cb) {
    dict_set_native(ctx, name, cb, false);
}

void tf_dict_set_native_copy(tf_ctx *ctx, const char *name, tf_native_fn cb) {
    dict_set_native(ctx, name, cb, true);
}

void tf_dict_add_native_scoped(tf_ctx *ctx, const char *name, size_t name_len,
                               size_t package_index, tf_native_fn cb,
                               const char *stack_effect,
                               const char *description) {
    assert(tf_dict_lookup_scoped(ctx, package_index, name, name_len) == NULL);
    tf_word *word = dict_insert_word(ctx, package_index, name, name_len, true,
                                     true);
    word->type = TF_WORD_NATIVE;
    word->native_impl = cb;
    dict_set_doc(word, stack_effect, description);
}

static bool name_is_local(const char *name, size_t len) {
    return name && len > 0 && memchr(name, '.', len) == NULL;
}

bool tf_dict_set_user_in_package(tf_ctx *ctx, size_t package_index,
                                 tf_obj *name, tf_obj *body) {
    if (!name_is_local(name->str.ptr, name->str.len)) {
        tf_ctx_runtime_errorf(ctx,
                              "'def' names must be local to the current package\n");
        return false;
    }

    tf_word *word = tf_dict_lookup_scoped(ctx, package_index, name->str.ptr,
                                          name->str.len);
    bool replacing = word != NULL;
    if (word) {
        if (word->type == TF_WORD_USER) tf_obj_release(word->user_impl);
        dict_clear_doc(word);
    } else {
        word = dict_insert_word(ctx, package_index, name->str.ptr,
                                name->str.len, true, true);
    }
    word->type = TF_WORD_USER;
    word->user_impl = body;
    tf_obj_retain(body);
    if (replacing) tf_dict_resolution_changed(ctx);
    return true;
}

bool tf_dict_set_user(tf_ctx *ctx, tf_obj *name, tf_obj *body) {
    return tf_dict_set_user_in_package(ctx, tf_current_package_index(ctx),
                                       name, body);
}

bool tf_dict_make_private_in_package(tf_ctx *ctx, size_t package_index,
                                     tf_obj *name) {
    if (package_index == TF_ROOT_PACKAGE) {
        tf_ctx_runtime_errorf(ctx,
                              "'private' is only valid in a package\n");
        return false;
    }
    if (!name_is_local(name->str.ptr, name->str.len)) {
        tf_ctx_runtime_errorf(ctx,
                              "'private' expected a local word name\n");
        return false;
    }
    tf_word *word = tf_dict_lookup_scoped(ctx, package_index, name->str.ptr,
                                          name->str.len);
    if (!word) {
        tf_ctx_runtime_errorf(ctx, "cannot make undefined package word '%s' private\n",
                              name->str.ptr);
        return false;
    }
    if (word->is_public) {
        word->is_public = false;
        tf_dict_resolution_changed(ctx);
    }
    return true;
}

bool tf_dict_make_private(tf_ctx *ctx, tf_obj *name) {
    return tf_dict_make_private_in_package(ctx,
                                           tf_current_package_index(ctx),
                                           name);
}

static bool split_qualified(const char *name, size_t len, size_t *prefix_len,
                            const char **local_name, size_t *local_len) {
    const char *separator = memchr(name, '.', len);
    if (!separator || separator == name || separator == name + len - 1) {
        return false;
    }
    size_t prefix = (size_t)(separator - name);
    if (memchr(separator + 1, '.', len - prefix - 1)) return false;
    *prefix_len = prefix;
    *local_name = separator + 1;
    *local_len = len - prefix - 1;
    return true;
}

static tf_word *dict_lookup_uncached(tf_ctx *ctx, size_t current_package,
                                     const char *name, size_t name_len) {
    tf_word *word = tf_dict_lookup_scoped(ctx, current_package, name,
                                          name_len);
    if (word) return word;
    if (current_package != TF_ROOT_PACKAGE) {
        word = tf_dict_lookup_scoped(ctx, TF_ROOT_PACKAGE, name, name_len);
        if (word && word->type == TF_WORD_NATIVE) return word;
    }

    size_t import_len = 0;
    const char *local_name = NULL;
    size_t local_len = 0;
    if (memchr(name, '.', name_len)) {
        if (!split_qualified(name, name_len, &import_len, &local_name,
                             &local_len)) {
            return NULL;
        }
        size_t target = tf_package_import_find(ctx, current_package, name,
                                               import_len);
        if (target == (size_t)-1) return NULL;
        const tf_package *package = tf_package_get(ctx, target);
        if (!package || package->state != TF_PACKAGE_LOADED) return NULL;
        word = tf_dict_lookup_scoped(ctx, target, local_name, local_len);
        return word && word->is_public ? word : NULL;
    }
    return NULL;
}

tf_word *tf_dict_lookup_from(tf_ctx *ctx, size_t current_package,
                             tf_obj *name) {
    if (!name || (tf_obj_typeof(name) != TF_OBJ_TYPE_SYMBOL &&
                  tf_obj_typeof(name) != TF_OBJ_TYPE_CALL)) {
        return NULL;
    }

    uintptr_t mixed_key = (uintptr_t)name >> 4;
    mixed_key ^= (uintptr_t)current_package;
    mixed_key ^= mixed_key >> 7;
    mixed_key ^= mixed_key >> 13;
    size_t slot = mixed_key & (TF_WORD_LOOKUP_CACHE_CAP - 1);
    tf_word_lookup_cache_entry *cached = &ctx->words.lookup_cache[slot];
    if (cached->key == name && cached->package_index == current_package &&
        cached->generation == ctx->words.resolution_generation &&
        cached->entry_index < ctx->words.count) {
        return &ctx->words.entries[cached->entry_index];
    }

    tf_word *word = dict_lookup_uncached(ctx, current_package, name->str.ptr,
                                         name->str.len);
    if (!word) return NULL;

    if (cached->key != name) {
        tf_obj_retain(name);
        if (cached->key) tf_obj_release(cached->key);
        cached->key = name;
    }
    cached->package_index = current_package;
    cached->generation = ctx->words.resolution_generation;
    cached->entry_index = (size_t)(word - ctx->words.entries);
    return word;
}

tf_word *tf_dict_lookup(tf_ctx *ctx, tf_obj *name) {
    return tf_dict_lookup_from(ctx, tf_current_package_index(ctx), name);
}

void tf_dict_each_visible(tf_ctx *ctx, tf_visible_word_fn visit,
                          void *userdata) {
    if (!ctx || !visit) return;
    size_t current = tf_current_package_index(ctx);
    for (size_t i = 0; i < ctx->words.count; i++) {
        tf_word *word = &ctx->words.entries[i];
        if (word->package_index == current ||
            (word->package_index == TF_ROOT_PACKAGE &&
             word->type == TF_WORD_NATIVE)) {
            visit(word->name, word->name_len, word, userdata);
            continue;
        }
        if (!word->is_public || word->package_index == TF_ROOT_PACKAGE) {
            continue;
        }
        for (size_t j = 0; j < ctx->package_imports.len; j++) {
            tf_package_import *imported = &ctx->package_imports.entries[j];
            if (imported->owner_package_index != current ||
                imported->target_package_index != word->package_index) {
                continue;
            }
            size_t len = imported->name_len + 1 + word->name_len;
            char *qualified = tf_xmalloc(len + 1);
            memcpy(qualified, imported->name, imported->name_len);
            qualified[imported->name_len] = '.';
            memcpy(qualified + imported->name_len + 1, word->name,
                   word->name_len);
            qualified[len] = '\0';
            visit(qualified, len, word, userdata);
            free(qualified);
        }
    }
}

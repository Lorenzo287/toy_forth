#include "tf_exec.h"

#include <stdint.h>
#include <stdio.h>
#ifdef TF_OBSERVE
#include <string.h>
#endif

#include "tf_random.h"

#if defined(_WIN32) && !defined(STB_LEAKCHECK)
#include <windows.h>
#endif

#ifdef STB_LEAKCHECK
#include "tf_alloc.h"
#endif

#define CHECK(condition, message)                                            \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "runtime state check failed: %s\n", message); \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static toy_state *nested_target = NULL;

static tf_obj *new_quick_cache_program(void) {
    tf_obj *program = tf_obj_new_vector();
    tf_vector_push(program, tf_obj_new_call("+", 1));
    return program;
}

static void cache_quick_program(tf_ctx *ctx, tf_obj *program) {
    tf_frame_push_program(ctx, program);
    tf_frame_pop(ctx, TF_OK);
}

static size_t quick_cache_find_set(tf_ctx *ctx, tf_obj *program) {
    for (size_t set = 0; set < TF_QUICK_PROGRAM_CACHE_SETS; set++) {
        for (size_t way = 0; way < TF_QUICK_PROGRAM_CACHE_WAYS; way++) {
            size_t slot = way == 0
                              ? set
                              : TF_QUICK_PROGRAM_CACHE_SETS +
                                    set * (TF_QUICK_PROGRAM_CACHE_WAYS - 1) +
                                    way - 1;
            tf_quick_program *quick = ctx->quick_programs[slot];
            if (quick && quick->program == program) return set;
        }
    }
    return SIZE_MAX;
}

static int check_quick_program_cache(void) {
    tf_ctx *ctx = tf_ctx_new(0, NULL);
    CHECK(ctx, "quick-program cache state creation");

    tf_obj *owned[TF_QUICK_PROGRAM_CACHE_SETS * 16] = {0};
    tf_obj *members[TF_QUICK_PROGRAM_CACHE_WAYS] = {0};
    size_t owned_len = 0;
    size_t target_set = SIZE_MAX;
    size_t member_count = 0;

    while (owned_len < sizeof(owned) / sizeof(owned[0]) &&
           member_count < TF_QUICK_PROGRAM_CACHE_WAYS) {
        tf_obj *program = new_quick_cache_program();
        owned[owned_len++] = program;
        cache_quick_program(ctx, program);

        size_t set = quick_cache_find_set(ctx, program);
        CHECK(set != SIZE_MAX, "new quick program remains cached");
        if (target_set == SIZE_MAX) target_set = set;
        if (set == target_set) members[member_count++] = program;
    }
    CHECK(member_count == TF_QUICK_PROGRAM_CACHE_WAYS,
          "fill every way in one cache set");
    for (size_t i = 0; i < member_count; i++) {
        CHECK(quick_cache_find_set(ctx, members[i]) != SIZE_MAX,
              "cache retains programs in every way");
    }

    cache_quick_program(ctx, members[0]);
    tf_obj *replacement = NULL;
    while (owned_len < sizeof(owned) / sizeof(owned[0]) && !replacement) {
        tf_obj *program = new_quick_cache_program();
        owned[owned_len++] = program;
        cache_quick_program(ctx, program);
        size_t set = quick_cache_find_set(ctx, program);
        CHECK(set != SIZE_MAX, "replacement quick program remains cached");
        if (set == target_set) {
            replacement = program;
        }
    }
    CHECK(replacement, "find one more program sharing the full cache set");
    CHECK(quick_cache_find_set(ctx, members[0]) != SIZE_MAX &&
              quick_cache_find_set(ctx, members[1]) == SIZE_MAX &&
              quick_cache_find_set(ctx, replacement) != SIZE_MAX,
          "cache evicts the least-recently-used way");

    for (size_t i = 0; i < owned_len; i++) tf_obj_release(owned[i]);
    tf_ctx_free(ctx);
    return 0;
}

static toy_status cached_native_one(toy_state *state) {
    return toy_push_int(state, 11);
}

static toy_status cached_native_two(toy_state *state) {
    return toy_push_int(state, 22);
}

static toy_status cached_native_replace_self(toy_state *state) {
    /* The public registration API requires an idle state; runtime def uses
     * the internal dictionary boundary while the VM is active. */
    tf_dict_set_native(state, "cached-native", cached_native_two);
    return toy_push_int(state, 33);
}

static bool cached_program_returns(tf_ctx *ctx, tf_obj *program,
                                    int64_t expected) {
    if (tf_vm_exec(ctx, program) != TF_OK) return false;
    int64_t value = 0;
    bool matches = toy_stack_size(ctx) == 1 &&
                   toy_get_int(ctx, 0, &value) && value == expected;
    toy_pop(ctx, toy_stack_size(ctx));
    return matches;
}

static int check_cached_native_targets(void) {
    tf_ctx *first = tf_ctx_new(0, NULL);
    tf_ctx *second = tf_ctx_new(0, NULL);
    CHECK(first && second, "native cache contexts");
    tf_obj *program = tf_obj_new_vector();
    tf_vector_push(program, tf_obj_new_call("cached-native", 13));

    CHECK(toy_register_word(first, "cached-native", cached_native_one) ==
              TOY_OK &&
              toy_register_word(second, "cached-native", cached_native_two) ==
                  TOY_OK,
          "register different native targets in two contexts");
    for (int i = 0; i < 3; i++) {
        CHECK(cached_program_returns(first, program, 11) &&
                  cached_program_returns(second, program, 22),
              "shared program caches native targets per context");
    }
    CHECK(toy_register_word(first, "cached-native", cached_native_two) ==
              TOY_OK &&
              cached_program_returns(first, program, 22),
          "native replacement invalidates a cached native target");
    CHECK(toy_eval(first, "<native-to-user>", "'cached-native [ 44 ] def") ==
              TOY_OK &&
              cached_program_returns(first, program, 44) &&
              cached_program_returns(first, program, 44),
          "a cached native target can become a user word");

    /* Force dictionary growth after caching a user target. */
    size_t old_capacity = first->words.entry_capacity;
    for (size_t i = 0; i < old_capacity; i++) {
        char name[48];
        snprintf(name, sizeof(name), "cache-padding-%zu", i);
        CHECK(toy_register_word(first, name, cached_native_one) == TOY_OK,
              "grow dictionary around a cached user target");
    }
    CHECK(first->words.entry_capacity > old_capacity &&
              cached_program_returns(first, program, 44),
          "cached user target survives dictionary reallocation");

    first->words.resolution_generation = SIZE_MAX - 1;
    CHECK(toy_register_word(first, "cached-native", cached_native_replace_self) ==
              TOY_OK &&
              cached_program_returns(first, program, 33) &&
              first->words.resolution_generation == 1 &&
              cached_program_returns(first, program, 22) &&
              cached_program_returns(second, program, 22),
          "native self-replacement clears active caches on generation wrap");

    tf_obj_release(program);
    tf_ctx_free(first);
    tf_ctx_free(second);
    return 0;
}

#ifdef TF_OBSERVE
static int check_runtime_metrics(void) {
    toy_state *state = toy_state_new(NULL);
    CHECK(state, "metrics state creation");
    CHECK(toy_eval(state, "<metrics>", "1 2 +") == TOY_OK,
          "metrics program execution");

    tf_runtime_metrics *metrics = &state->metrics;
    CHECK(metrics->instructions == 3 && metrics->call_instructions == 1 &&
              metrics->native_continuation_steps == 0 &&
              metrics->native_word_calls == 1 &&
              metrics->user_word_calls == 0 && metrics->program_frames == 1 &&
              metrics->native_frames == 0 && metrics->max_frame_depth == 1 &&
              metrics->max_data_stack_depth == 2 &&
              metrics->dictionary_lookups == 1 &&
              metrics->dictionary_cache_hits == 0 &&
              metrics->dictionary_cache_misses == 1 &&
              metrics->quick_program_cache_hits == 0 &&
              metrics->quick_program_cache_misses == 1 &&
              metrics->quick_program_cache_evictions == 0 &&
              metrics->quickened_lookup_hits == 0 &&
              metrics->quickened_lookup_misses == 1 &&
              metrics->specialized_dispatches == 1 &&
              metrics->general_dispatches == 0,
          "deterministic metrics counters");

    FILE *output = tmpfile();
    CHECK(output, "metrics temporary output");
    CHECK(tf_metrics_write_json(state, output), "write metrics JSON");
    CHECK(fflush(output) == 0 && fseek(output, 0, SEEK_SET) == 0,
          "rewind metrics JSON");
    char json[2048];
    size_t length = fread(json, 1, sizeof(json) - 1, output);
    json[length] = '\0';
    CHECK(!ferror(output) &&
              strstr(json, "\"schema\": \"toy.runtime-metrics\"") &&
              strstr(json, "\"instructions\": 3") &&
              strstr(json, "\"specialized_dispatches\": 1"),
          "metrics JSON schema and values");
    fclose(output);
    toy_state_free(state);

    state = toy_state_new(NULL);
    CHECK(state, "repeat metrics state creation");
    CHECK(toy_eval(state, "<repeat-metrics>", "0 5 [ 1 + ] times") ==
              TOY_OK,
          "repeat metrics program execution");
    int64_t result = 0;
    CHECK(toy_get_int(state, 0, &result) && result == 5,
          "repeat metrics result");
    metrics = &state->metrics;
    CHECK(metrics->native_continuation_steps == 0 &&
              metrics->program_frames == 2 && metrics->native_frames == 0,
          "times reuses one program frame");
    toy_state_free(state);
    return 0;
}
#endif

static toy_status run_nested_state(toy_state *state) {
    (void)state;
    if (!nested_target) return TOY_ERROR;
    return toy_eval(nested_target, "<nested-target>",
                    "0 64 range >list [ 1 + ] map empty");
}

static int check_pcg_sequence(void) {
    tf_random random = TF_RANDOM_INIT;
    tf_random_seed(&random, UINT64_C(42), UINT64_C(54));
    const uint32_t expected[] = {
        UINT32_C(0xa15c02b7), UINT32_C(0x7b47f409), UINT32_C(0xba1d3330),
        UINT32_C(0x83d2f293), UINT32_C(0xbfa4784b), UINT32_C(0xcbed606e),
    };
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        CHECK(tf_random_u32(&random) == expected[i],
              "documented PCG32 sequence");
    }
    return 0;
}

static int check_random_ranges(void) {
    tf_random random = TF_RANDOM_INIT;
    tf_random_seed(&random, UINT64_C(42), UINT64_C(54));
    const int64_t expected[] = {65, 59, 46, 5, 48};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        int64_t value = 0;
        CHECK(tf_random_int_range(&random, 0, 100, &value),
              "generate bounded integer");
        CHECK(value == expected[i], "deterministic bounded sequence");
    }

    int64_t invalid_result = 0;
    CHECK(!tf_random_int_range(&random, 0, 0, &invalid_result),
          "reject empty integer range");
    CHECK(!tf_random_int_range(&random, 1, 0, &invalid_result),
          "reject reversed integer range");

    const struct {
        int64_t lower;
        int64_t upper;
    } ranges[] = {
        {-50, 75},
        {0, 1},
        {INT64_MIN, INT64_MAX},
        {INT64_MIN, INT64_MIN + 1},
        {INT64_MAX - 1, INT64_MAX},
    };
    for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
        for (size_t draw = 0; draw < 256; draw++) {
            int64_t value = 0;
            CHECK(tf_random_int_range(&random, ranges[i].lower,
                                      ranges[i].upper, &value),
                  "generate across signed range");
            CHECK(value >= ranges[i].lower && value < ranges[i].upper,
                  "bounded integer remains in half-open range");
        }
    }
    return 0;
}

#if defined(_WIN32) && !defined(STB_LEAKCHECK)
static DWORD WINAPI run_state_worker(void *userdata) {
    (void)userdata;
    toy_state *state = toy_state_new(NULL);
    if (!state) return 1;
    for (size_t i = 0; i < 20; i++) {
        if (toy_eval(state, "<concurrent-state>",
                     "0 1000 range >list [ 1 + ] map "
                     "0 swap [ + ] fold") != TOY_OK) {
            toy_state_free(state);
            return 1;
        }
        int64_t result = 0;
        if (!toy_get_int(state, 0, &result) || result != 500500 ||
            !toy_pop(state, 1)) {
            toy_state_free(state);
            return 1;
        }
    }
    toy_state_free(state);
    return 0;
}

static int check_concurrent_states(void) {
    HANDLE workers[4] = {0};
    size_t started = 0;
    for (; started < sizeof(workers) / sizeof(workers[0]); started++) {
        workers[started] =
            CreateThread(NULL, 0, run_state_worker, NULL, 0, NULL);
        if (!workers[started]) break;
    }
    if (started != sizeof(workers) / sizeof(workers[0])) {
        if (started != 0) {
            WaitForMultipleObjects((DWORD)started, workers, TRUE, INFINITE);
        }
        for (size_t i = 0; i < started; i++) CloseHandle(workers[i]);
        return 1;
    }

    if (WaitForMultipleObjects((DWORD)started, workers, TRUE, INFINITE) ==
        WAIT_FAILED) {
        for (size_t i = 0; i < started; i++) CloseHandle(workers[i]);
        return 1;
    }
    int result = 0;
    for (size_t i = 0; i < started; i++) {
        DWORD exit_code = 1;
        if (!GetExitCodeThread(workers[i], &exit_code) || exit_code != 0) {
            result = 1;
        }
        CloseHandle(workers[i]);
    }
    return result;
}
#endif

int main(void) {
#ifdef TF_OBSERVE
    CHECK(check_runtime_metrics() == 0, "runtime metrics");
#endif
    CHECK(check_quick_program_cache() == 0, "quick-program cache");
    CHECK(check_cached_native_targets() == 0, "cached native targets");
    CHECK(check_pcg_sequence() == 0, "PCG32 implementation");
    CHECK(check_random_ranges() == 0, "unbiased integer ranges");

    toy_state *first = toy_state_new(NULL);
    toy_state *second = toy_state_new(NULL);
    CHECK(first && second, "state creation");
    CHECK(first->random.increment & UINT64_C(1), "first stream is valid");
    CHECK(second->random.increment & UINT64_C(1), "second stream is valid");
    CHECK(first->random.state != second->random.state ||
              first->random.increment != second->random.increment,
          "states receive independent system seeds");

    toy_random_seed(first, 42);
    toy_random_seed(second, 42);
    int64_t first_value = 0;
    int64_t second_value = 0;
    CHECK(toy_random_int(first, 0, 100, &first_value) && first_value == 65,
          "public random service uses the state's PCG32 stream");
    CHECK(toy_random_int(second, 0, 100, &second_value) &&
              second_value == first_value,
          "advancing one state does not affect another");
    toy_state *third = toy_state_new(NULL);
    CHECK(third, "third state creation");
    CHECK(toy_random_int(first, 0, 100, &first_value) &&
              toy_random_int(second, 0, 100, &second_value) &&
              first_value == second_value,
          "creating another state does not reseed existing states");
    toy_state_free(third);

    CHECK(first->control_state_cache_len == 0 &&
              second->control_state_cache_len == 0,
          "continuation caches start empty");
    CHECK(toy_eval(first, "<first-control>", "1 [ 2 ] dip empty") ==
              TOY_OK,
          "populate first continuation cache");
    CHECK(first->control_state_cache_len != 0,
          "first state retains its continuation storage");
    CHECK(second->control_state_cache_len == 0,
          "second continuation cache remains independent");

    size_t first_cache_len = first->control_state_cache_len;
    nested_target = second;
    CHECK(toy_register_word(first, "run-nested-state", run_nested_state) ==
              TOY_OK,
          "register nested-state callback");
    CHECK(toy_eval(first, "<nested-caller>",
                   "run-nested-state 0 64 range >list") == TOY_OK,
          "execute another state and resume the caller's allocation scope");
    toy_state_free(second);
    nested_target = NULL;
    CHECK(first->control_state_cache_len == first_cache_len,
          "closing another state preserves the first cache");
    CHECK(toy_eval(first, "<nested-result>", "len") == TOY_OK &&
              toy_get_int(first, 0, &first_value) && first_value == 64 &&
              toy_pop(first, 1),
          "caller-owned values survive nested-state teardown");
    CHECK(toy_eval(first, "<first-after-close>", "3 [ 4 ] dip empty") ==
              TOY_OK,
          "first state remains usable after another closes");
    toy_state_free(first);

#if defined(_WIN32) && !defined(STB_LEAKCHECK)
    CHECK(check_concurrent_states() == 0, "concurrent independent states");
#endif

#ifdef STB_LEAKCHECK
    stb_leakcheck_dumpmem();
#endif
    return 0;
}

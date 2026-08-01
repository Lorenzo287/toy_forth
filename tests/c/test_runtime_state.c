#include "tf_exec.h"

#include <stdint.h>
#include <stdio.h>

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
    CHECK(check_pcg_sequence() == 0, "PCG32 implementation");

    toy_state *first = toy_state_new(NULL);
    toy_state *second = toy_state_new(NULL);
    CHECK(first && second, "state creation");
    CHECK(first->random.increment & UINT64_C(1), "first stream is valid");
    CHECK(second->random.increment & UINT64_C(1), "second stream is valid");
    CHECK(first->random.state != second->random.state ||
              first->random.increment != second->random.increment,
          "states receive independent system seeds");

    tf_random_seed(&first->random, UINT64_C(42), UINT64_C(54));
    tf_random_seed(&second->random, UINT64_C(42), UINT64_C(54));
    CHECK(toy_eval(first, "<first-random>", "rand") == TOY_OK,
          "evaluate first random word");
    int64_t value = 0;
    CHECK(toy_get_int(first, 0, &value) &&
              value == (int64_t)UINT32_C(0xa15c02b7),
          "rand uses the state's PCG32 stream");
    CHECK(toy_pop(first, 1), "pop first random value");

    CHECK(tf_random_u32(&second->random) == UINT32_C(0xa15c02b7),
          "advancing one state does not affect another");
    toy_state *third = toy_state_new(NULL);
    CHECK(third, "third state creation");
    CHECK(tf_random_u32(&second->random) == UINT32_C(0x7b47f409),
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
              toy_get_int(first, 0, &value) && value == 64 &&
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

#include "toy.h"

#ifdef TF_ALLOC_STATS
#include "tf_alloc.h"
#endif

#include <inttypes.h>
#include <stdio.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

enum { DEFERRED_BURST_SIZE = 100000 };

static uint64_t monotonic_ns(void) {
#ifdef _WIN32
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    uint64_t seconds = (uint64_t)(counter.QuadPart / frequency.QuadPart);
    uint64_t remainder = (uint64_t)(counter.QuadPart % frequency.QuadPart);
    return seconds * UINT64_C(1000000000) +
           remainder * UINT64_C(1000000000) / (uint64_t)frequency.QuadPart;
#else
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) +
           (uint64_t)time.tv_nsec;
#endif
}

static int fail(toy_state *state, toy_value *handler,
                const char *operation) {
    fprintf(stderr, "%s failed: %s\n", operation,
            toy_get_error(state) ? toy_get_error(state) : "invalid state");
    toy_value_release(handler);
    toy_state_free(state);
    return 1;
}

int main(void) {
    toy_state *state = toy_state_new(NULL);
    if (!state) {
        fputs("creating Toy state failed\n", stderr);
        return 1;
    }
    if (toy_eval(state, "deferred-benchmark.toy", "[ drop ]") != TOY_OK) {
        return fail(state, NULL, "creating deferred handler");
    }

    toy_value *handler = toy_value_retain(state, 0);
    if (!handler || !toy_pop(state, 1)) {
        return fail(state, handler, "retaining deferred handler");
    }

    uint64_t queue_start = monotonic_ns();
    toy_status status = TOY_OK;
    for (int64_t i = 0; i < DEFERRED_BURST_SIZE && status == TOY_OK; ++i) {
        status = toy_push_int(state, i);
        if (status == TOY_OK) status = toy_defer_call(state, handler, 1);
    }
    uint64_t queue_elapsed = monotonic_ns() - queue_start;
    if (status != TOY_OK ||
        toy_deferred_count(state) != DEFERRED_BURST_SIZE ||
        toy_stack_size(state) != 0) {
        return fail(state, handler, "queueing deferred burst");
    }

    uint64_t drain_start = monotonic_ns();
    status = toy_run_deferred(state);
    uint64_t drain_elapsed = monotonic_ns() - drain_start;
    if (status != TOY_OK || toy_deferred_count(state) != 0 ||
        toy_stack_size(state) != 0) {
        return fail(state, handler, "draining deferred burst");
    }

    printf("--- Deferred Call Burst ---\n");
    printf("Queue %d calls: %" PRIu64 " ns (%.1f ns/call)\n",
           DEFERRED_BURST_SIZE, queue_elapsed,
           (double)queue_elapsed / DEFERRED_BURST_SIZE);
    printf("Drain %d calls: %" PRIu64 " ns (%.1f ns/call)\n",
           DEFERRED_BURST_SIZE, drain_elapsed,
           (double)drain_elapsed / DEFERRED_BURST_SIZE);

    toy_value_release(handler);
    toy_state_free(state);
#ifdef TF_ALLOC_STATS
    tf_alloc_stats_dump();
#endif
    return 0;
}

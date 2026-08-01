#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "tf_random.h"

#include <stddef.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <stdlib.h>

/* UCRT exposes rand_s only when _CRT_RAND_S is defined before stdlib.h. It is
 * used solely as an OS-backed entropy source; PCG32 produces the stream. */
#ifdef __cplusplus
extern "C" int __cdecl rand_s(unsigned int *value);
#else
extern int __cdecl rand_s(unsigned int *value);
#endif
#elif defined(__unix__) || defined(__APPLE__)
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "tf_lock.h"

#define TF_RANDOM_PCG_MULTIPLIER UINT64_C(6364136223846793005)

static tf_lock fallback_seed_lock = TF_LOCK_INIT;
static uint64_t fallback_seed_counter = 0;

uint32_t tf_random_u32(tf_random *random) {
    if (!random) return 0;

    uint64_t old_state = random->state;
    random->state = old_state * TF_RANDOM_PCG_MULTIPLIER + random->increment;

    uint32_t xorshifted =
        (uint32_t)(((old_state >> 18u) ^ old_state) >> 27u);
    uint32_t rotation = (uint32_t)(old_state >> 59u);
    return (xorshifted >> rotation) |
           (xorshifted << ((0u - rotation) & 31u));
}

void tf_random_seed(tf_random *random, uint64_t seed, uint64_t stream) {
    if (!random) return;

    random->state = 0;
    random->increment = (stream << 1u) | UINT64_C(1);
    (void)tf_random_u32(random);
    random->state += seed;
    (void)tf_random_u32(random);
}

static bool system_random_bytes(void *data, size_t size) {
    if (!data && size != 0) return false;
    if (size == 0) return true;

#ifdef _WIN32
    unsigned char *cursor = data;
    while (size != 0) {
        unsigned int value = 0;
        if (rand_s(&value) != 0) return false;
        size_t chunk = size < sizeof(value) ? size : sizeof(value);
        memcpy(cursor, &value, chunk);
        cursor += chunk;
        size -= chunk;
    }
    return true;
#elif defined(__unix__) || defined(__APPLE__)
    int descriptor = open("/dev/urandom", O_RDONLY);
    if (descriptor < 0) return false;

    unsigned char *cursor = data;
    size_t remaining = size;
    while (remaining != 0) {
        size_t chunk = remaining < 1024u * 1024u
                           ? remaining
                           : 1024u * 1024u;
        ssize_t count = read(descriptor, cursor, chunk);
        if (count > 0) {
            cursor += (size_t)count;
            remaining -= (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        (void)close(descriptor);
        return false;
    }
    return close(descriptor) == 0;
#else
    (void)data;
    return false;
#endif
}

bool tf_random_seed_system(tf_random *random) {
    if (!random) return false;

    uint64_t seeds[2];
    if (!system_random_bytes(seeds, sizeof(seeds))) return false;
    tf_random_seed(random, seeds[0], seeds[1]);
    return true;
}

static uint64_t mix_seed(uint64_t value) {
    value ^= value >> 30u;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27u;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31u);
}

void tf_random_init(tf_random *random, const void *uniqueness) {
    if (!random) return;
    *random = (tf_random)TF_RANDOM_INIT;
    if (tf_random_seed_system(random)) return;

    /* Entropy failure must not restore the old same-second global behavior.
     * Mix independent clocks, an address, and a synchronized creation counter
     * to give each state a distinct best-effort fallback stream. */
    struct timespec wall = {0};
    (void)timespec_get(&wall, TIME_UTC);
    tf_lock_acquire(&fallback_seed_lock);
    uint64_t counter = ++fallback_seed_counter;
    tf_lock_release(&fallback_seed_lock);

    uint64_t seed = (uint64_t)wall.tv_sec ^
                    ((uint64_t)wall.tv_nsec << 32u) ^
                    (uint64_t)(uintptr_t)uniqueness ^
                    ((uint64_t)clock() << 1u) ^ counter;
    tf_random_seed(random, mix_seed(seed),
                   mix_seed(seed ^ UINT64_C(0x9e3779b97f4a7c15)));
}

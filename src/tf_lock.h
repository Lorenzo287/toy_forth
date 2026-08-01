#ifndef TF_LOCK_H
#define TF_LOCK_H

/* Tiny internal lock for rare process-wide bookkeeping such as fallback seed
 * uniqueness. The supported Windows compilers share the Win32 interlocked
 * API; GCC and Clang provide atomic builtins on other supported platforms. */
#ifdef _WIN32
#include <windows.h>

typedef struct {
    volatile LONG value;
} tf_lock;

#define TF_LOCK_INIT {0}

static inline void tf_lock_acquire(tf_lock *lock) {
    while (InterlockedCompareExchange(&lock->value, 1, 0) != 0) {
        SwitchToThread();
    }
}

static inline void tf_lock_release(tf_lock *lock) {
    InterlockedExchange(&lock->value, 0);
}
#else
typedef struct {
    unsigned char value;
} tf_lock;

#define TF_LOCK_INIT {0}

static inline void tf_lock_acquire(tf_lock *lock) {
    while (__atomic_test_and_set(&lock->value, __ATOMIC_ACQUIRE)) {
    }
}

static inline void tf_lock_release(tf_lock *lock) {
    __atomic_clear(&lock->value, __ATOMIC_RELEASE);
}
#endif

#endif  // TF_LOCK_H

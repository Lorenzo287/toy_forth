#ifndef TF_RANDOM_H
#define TF_RANDOM_H

#include <stdbool.h>
#include <stdint.h>

/* Explicit PCG32 XSH-RR state. Each interpreter owns one instance, so creating
 * or running another state cannot perturb its sequence. This is a general-
 * purpose pseudorandom generator, not a cryptographic generator. */
typedef struct {
    uint64_t state;
    uint64_t increment;
} tf_random;

#define TF_RANDOM_INIT                                                       \
    {UINT64_C(6364136223846793006), UINT64_C(1)}

void tf_random_init(tf_random *random, const void *uniqueness);
void tf_random_seed(tf_random *random, uint64_t seed, uint64_t stream);
bool tf_random_seed_system(tf_random *random);
uint32_t tf_random_u32(tf_random *random);

#endif  // TF_RANDOM_H

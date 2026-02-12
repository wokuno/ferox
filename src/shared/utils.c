#include "utils.h"
#include <stdlib.h>
#include <time.h>

static uint64_t rng_state = 0;

// xorshift64 algorithm for fast, decent quality random numbers
static uint64_t xorshift64(void) {
    if (rng_state == 0) {
        rng_state = (uint64_t)time(NULL);
    }
    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

void rng_seed(uint64_t seed) {
    rng_state = seed;
    if (rng_state == 0) {
        rng_state = 1;  // Avoid zero state
    }
}

float rand_float(void) {
    return (float)(xorshift64() & 0xFFFFFFFF) / (float)0xFFFFFFFF;
}

int rand_int(int max) {
    if (max <= 0) return 0;
    return (int)(xorshift64() % (uint64_t)max);
}

int rand_range(int min, int max) {
    if (min >= max) return min;
    return min + rand_int(max - min + 1);
}

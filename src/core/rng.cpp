#include "core/rng.h"

u32 rng_next(u32* state) {
    if (*state == 0) {
        *state = 1;
    }
    u32 x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

u32 rng_range(u32* state, u32 exclusive_max) {
    if (exclusive_max == 0) {
        return 0;
    }
    return rng_next(state) % exclusive_max;
}

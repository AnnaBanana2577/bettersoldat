// The game's own randomness, so every world rolls the same: xorshift64*, from
// World.rng, from a soldier's own rng for what its player rolls, and from a bullet's own
// numbers for what happens to it in flight.

#include "game/systems/systems.h"

uint64_t rand_next(uint64_t *state)
{
    uint64_t x = *state;
    if (x == 0) x = 0x9E3779B97F4A7C15ull;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1Dull;
}

float rand_f32(uint64_t *state)
{
    return (float)(rand_next(state) >> 40) / (float)(1 << 24);
}

int rand_int(uint64_t *state, int n)
{
    if (n <= 0) return 0;
    return (int)(rand_next(state) % (uint64_t)n);
}

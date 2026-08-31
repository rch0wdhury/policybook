#include "policybook/rng.h"

#include <assert.h>
#include <stddef.h>

#define PB_GAMMA 0x9E3779B9u
#define PB_MUL_1 0x21F0AAADu
#define PB_MUL_2 0x735A2D97u

/* 2^32 as a double. Exact. */
#define PB_TWO_POW_32 4294967296.0

/* The splitmix32 finalising mix. */
static uint32_t pb_finalise(uint32_t z)
{
    z ^= z >> 16;
    z *= PB_MUL_1;
    z ^= z >> 15;
    z *= PB_MUL_2;
    z ^= z >> 15;
    return z;
}

static uint32_t pb_rotl(uint32_t x, unsigned k)
{
    return (uint32_t)((x << k) | (x >> (32u - k)));
}

uint32_t pb_mix32(uint32_t value)
{
    return pb_finalise(value);
}

void pb_rng_init(pb_rng *rng, uint32_t seed)
{
    uint32_t state = seed;
    int i;

    assert(rng != NULL);

    /* splitmix32 expands the single seed word into the four state words. */
    for (i = 0; i < 4; ++i) {
        state += PB_GAMMA;
        rng->s[i] = pb_finalise(state);
    }

    /* xoshiro cannot start from all zeroes; it would emit zeroes forever. */
    if ((rng->s[0] | rng->s[1] | rng->s[2] | rng->s[3]) == 0u) {
        rng->s[0] = 1u;
    }
}

uint32_t pb_rng_next_u32(pb_rng *rng)
{
    uint32_t s1 = rng->s[1];
    uint32_t result;
    uint32_t t;
    uint32_t s2;
    uint32_t s3;

    /* The "**" scrambler: rotl(s1 * 5, 7) * 9. */
    result = pb_rotl(s1 * 5u, 7u) * 9u;

    t = (uint32_t)(s1 << 9);
    s2 = rng->s[2] ^ rng->s[0];
    s3 = rng->s[3] ^ s1;
    rng->s[1] = s1 ^ s2;
    rng->s[0] = rng->s[0] ^ s3;
    rng->s[2] = s2 ^ t;
    rng->s[3] = pb_rotl(s3, 11u);

    return result;
}

double pb_rng_next_float(pb_rng *rng)
{
    return (double)pb_rng_next_u32(rng) / PB_TWO_POW_32;
}

uint32_t pb_rng_next_int(pb_rng *rng, uint32_t bound)
{
    uint32_t threshold;
    uint32_t value;

    assert(bound >= 1u);

    /*
     * Discard the short tail so the remaining range is an exact multiple of
     * bound. (0u - bound) is 2^32 - bound in unsigned arithmetic.
     */
    threshold = (0u - bound) % bound;
    value = pb_rng_next_u32(rng);
    while (value < threshold) {
        value = pb_rng_next_u32(rng);
    }
    return value % bound;
}

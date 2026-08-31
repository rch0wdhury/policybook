/*
 * pb_rng — the deterministic random number generator shared by every Policybook
 * policy, in every language.
 *
 * This is the C side of the generator defined in `packages/core/src/rng.ts`. It
 * must agree with it bit for bit: same seed, same stream, in TypeScript, Python
 * and C. `tests/gen/test_rng.c` is generated from the shared
 * reference vectors and is the proof.
 *
 * The generator is xoshiro128** (Blackman and Vigna) seeded by splitmix32. It
 * is fast and has a 2^128 - 1 period. It is NOT cryptographically secure.
 *
 * Policies never call rand(); they receive a pb_rng at create time.
 */

#ifndef POLICYBOOK_RNG_H
#define POLICYBOOK_RNG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Generator state. Copyable: copying a pb_rng forks the stream. */
typedef struct pb_rng {
    uint32_t s[4];
} pb_rng;

/*
 * Seed a generator.
 *
 * Every seed, including 0, produces a valid stream.
 */
void pb_rng_init(pb_rng *rng, uint32_t seed);

/* The next 32 random bits. */
uint32_t pb_rng_next_u32(pb_rng *rng);

/*
 * A double in [0, 1) with 32 bits of resolution.
 *
 * Deliberately not the 53-bit variant: one next_u32 divided by 2^32 is exactly
 * the same number in all three languages, with no rounding argument to have.
 */
double pb_rng_next_float(pb_rng *rng);

/*
 * A uniform integer in [0, bound), with no modulo bias.
 *
 * `bound` must be at least 1. Note that the TypeScript and Python versions
 * accept a bound of 2^32, which uint32_t cannot express; C therefore tops out
 * at 2^32 - 1. No vector uses a bound anywhere near that.
 */
uint32_t pb_rng_next_int(pb_rng *rng, uint32_t bound);

/*
 * Hash a key into a well-distributed 32-bit value.
 *
 * The canonical key hash for the registry — sketch indices, virtual-node
 * placement, anything that needs to scatter keys. It is the splitmix32
 * finaliser, the same mix used to expand a seed.
 */
uint32_t pb_mix32(uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RNG_H */

/*
 * The cache fuzzer's invariants, as an ordinary test.
 *
 * libFuzzer finds the strange inputs, but it only runs when someone asks and
 * only where a fuzzer runtime exists. This driver feeds the same body
 * pseudo-random sequences from the registry's own deterministic generator, so
 * every policy's contract — evict returns a key it holds, capacity is never
 * exceeded, memory never grows after create — is checked on every build, on
 * every platform, in a second.
 *
 * Deterministic by construction: a failure here reproduces exactly.
 */

#include <stdint.h>
#include <stdio.h>

#include "policybook/rng.h"

#include "fuzz_cache_core.h"

/* Each round is one decoded call sequence. */
#define PB_FUZZ_ROUNDS 4000u
#define PB_FUZZ_MAX_INPUT 512u

int main(void)
{
    pb_rng rng;
    uint8_t input[PB_FUZZ_MAX_INPUT];
    unsigned round;
    unsigned long long bytes = 0;
    int violations = 0;

    pb_rng_init(&rng, 20260829u);

    for (round = 0; round < PB_FUZZ_ROUNDS; ++round) {
        /* A short input exercises a cache that never fills; a long one drives it
         * through many evictions. Both matter, so the length varies. */
        uint32_t length = 3u + pb_rng_next_int(&rng, PB_FUZZ_MAX_INPUT - 3u);
        uint32_t i;

        for (i = 0; i < length; ++i) {
            input[i] = (uint8_t)pb_rng_next_int(&rng, 256u);
        }

        violations += pb_fuzz_cache_once(input, (size_t)length);
        bytes += length;
    }

    if (violations > 0) {
        fprintf(stderr, "fuzz_cache_standalone: %d invariant violation(s)\n", violations);
        return 1;
    }

    printf("fuzz_cache_standalone: %u rounds over %u policies, %llu bytes, no violations\n",
           PB_FUZZ_ROUNDS, pb_fuzz_cache_policy_count(), bytes);
    return 0;
}

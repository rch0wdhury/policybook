/*
 * The kv-cache fuzzer's invariants, as an ordinary test.
 *
 * libFuzzer finds the strange inputs, but it only runs when someone asks and
 * only where a fuzzer runtime exists. This driver feeds the same body
 * pseudo-random sequences from the registry's own deterministic generator, so
 * every policy's contract — the budget is respected, victims are real, nothing
 * allocates after create — is checked on every build, on every platform, in a
 * second.
 *
 * Deterministic by construction: a failure here reproduces exactly.
 */

#include <stdint.h>
#include <stdio.h>

#include "policybook/rng.h"

#include "fuzz_kvcache_core.h"

/* Each round is one decoded decode loop. */
#define PB_FUZZ_KV_ROUNDS 2000u
#define PB_FUZZ_KV_MAX_INPUT 256u

int main(void)
{
    pb_rng rng;
    uint8_t input[PB_FUZZ_KV_MAX_INPUT];
    unsigned round;
    unsigned long long bytes = 0;
    int violations = 0;

    pb_rng_init(&rng, 20260829u);

    for (round = 0; round < PB_FUZZ_KV_ROUNDS; ++round) {
        /* A short input runs a cache that never fills; a long one drives it far
         * past the budget and keeps it there. Both matter, so the length
         * varies. */
        uint32_t length = 4u + pb_rng_next_int(&rng, PB_FUZZ_KV_MAX_INPUT - 4u);
        uint32_t i;

        for (i = 0; i < length; ++i) {
            input[i] = (uint8_t)pb_rng_next_int(&rng, 256u);
        }

        violations += pb_fuzz_kvcache_once(input, (size_t)length);
        bytes += length;
    }

    if (violations > 0) {
        fprintf(stderr, "fuzz_kvcache_standalone: %d invariant violation(s)\n", violations);
        return 1;
    }

    printf("fuzz_kvcache_standalone: %u rounds over %u policies, %llu bytes, no violations\n",
           PB_FUZZ_KV_ROUNDS, pb_fuzz_kvcache_policy_count(), bytes);
    return 0;
}

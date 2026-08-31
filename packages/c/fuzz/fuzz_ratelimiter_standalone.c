/*
 * The rate-limiter fuzzer's invariants, as an ordinary test.
 *
 * libFuzzer finds the strange inputs, but it only runs when someone asks and
 * only where a fuzzer runtime exists. This driver feeds the same body
 * pseudo-random sequences from the registry's own deterministic generator, so
 * every policy's contract — retry_after is honest, state stays inside max_keys,
 * memory never grows after create — is checked on every build, on every
 * platform, in a second.
 *
 * Deterministic by construction: a failure here reproduces exactly.
 */

#include <stdint.h>
#include <stdio.h>

#include "policybook/rng.h"

#include "fuzz_ratelimiter_core.h"

/* Each round is one decoded call sequence. */
#define PB_FUZZ_RL_ROUNDS 4000u
#define PB_FUZZ_RL_MAX_INPUT 256u

int main(void)
{
    pb_rng rng;
    uint8_t input[PB_FUZZ_RL_MAX_INPUT];
    unsigned round;
    unsigned long long bytes = 0;
    int violations = 0;

    pb_rng_init(&rng, 20260829u);

    for (round = 0; round < PB_FUZZ_RL_ROUNDS; ++round) {
        /* A short input exercises a limiter that never refuses; a long one
         * drives it well past its budget. Both matter, so the length varies. */
        uint32_t length = 5u + pb_rng_next_int(&rng, PB_FUZZ_RL_MAX_INPUT - 5u);
        uint32_t i;

        for (i = 0; i < length; ++i) {
            input[i] = (uint8_t)pb_rng_next_int(&rng, 256u);
        }

        violations += pb_fuzz_ratelimiter_once(input, (size_t)length);
        bytes += length;
    }

    if (violations > 0) {
        fprintf(stderr, "fuzz_ratelimiter_standalone: %d invariant violation(s)\n", violations);
        return 1;
    }

    printf(
        "fuzz_ratelimiter_standalone: %u rounds over %u policies, %llu bytes, no violations\n",
        PB_FUZZ_RL_ROUNDS, pb_fuzz_ratelimiter_policy_count(), bytes);
    return 0;
}

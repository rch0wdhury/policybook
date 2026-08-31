/*
 * Exponential backoff with full jitter — retry policy in use.
 *
 * Build (against the installed library):
 *     cc -std=c99 retry_full_jitter.c -lpolicybook -lm -o retry_full_jitter
 *
 * The smallest decision in the registry and the one most often got wrong. A
 * request failed; when should the client come back?
 *
 * The delay *curve* is the part everyone already knows. The part that decides
 * outcomes is where the randomness goes: backing off exponentially without
 * jitter converts a continuous herd into a periodic one rather than dispersing
 * it, so every client that failed together still returns together. Drawing
 * uniformly from `[0, ceiling]` is what actually spreads a fleet out.
 *
 * The `pb_rng` is supplied at create, as everywhere else here — a policy that
 * reached for a global source could not be replayed.
 */

#include <stdio.h>

#include "policybook/retry/exponential_full_jitter.h"
#include "policybook/retry/retry.h"
#include "policybook/rng.h"

int main(void)
{
    pb_retry_exponential_full_jitter_params params =
        PB_RETRY_EXPONENTIAL_FULL_JITTER_PARAMS_DEFAULT;
    pb_retry_error error = PB_RETRY_ERROR_DEFAULT; /* a retryable 503 */
    pb_rng rng;
    pb_retry *policy;
    uint32_t attempt;
    uint64_t elapsed = 0;

    /* Seeded explicitly: the same seed gives the same delays, in this language
     * and in the other two. */
    pb_rng_init(&rng, 42u);

    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 8u;
    policy = pb_retry_exponential_full_jitter.create(&params, NULL, &rng);
    if (policy == NULL) {
        fprintf(stderr, "could not create the policy\n");
        return 1;
    }

    printf("exponential full jitter, base %u ms, cap %u ms, %u attempts:\n", params.base_ms,
           params.cap_ms, params.max_attempts);

    for (attempt = 1u;; ++attempt) {
        /* `attempt` is 1-based and is the number of the attempt that just
         * failed, so the first call always passes 1. */
        int64_t delay = pb_retry_exponential_full_jitter.next_delay(policy, attempt, &error);

        if (delay == PB_RETRY_GIVE_UP) {
            /* A decision, not an error — and negative so it can never be
             * confused with a delay, which is why this returns a signed type. */
            printf("  attempt %u: give up after %lu ms of waiting\n", attempt,
                   (unsigned long)elapsed);
            break;
        }

        elapsed += (uint64_t)delay;
        printf("  attempt %u: wait %5ld ms  (ceiling %lu ms)\n", attempt, (long)delay,
               (unsigned long)(params.base_ms << (attempt - 1u) > params.cap_ms
                                   ? params.cap_ms
                                   : params.base_ms << (attempt - 1u)));
    }

    printf("\nEach delay is drawn from [0, ceiling], so two clients that failed together\n"
           "come back at different moments. That is the whole point: on the canonical\n"
           "workload it cuts the peak simultaneous retries roughly sixfold against the\n"
           "same curve without jitter.\n");

    pb_retry_exponential_full_jitter.destroy(policy);
    return 0;
}

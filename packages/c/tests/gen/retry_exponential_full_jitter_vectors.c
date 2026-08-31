/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for retry/exponential-full-jitter, produced by scripts/gen-c-vectors.ts from
 * policies/retry/exponential-full-jitter/vectors.json. Regenerate with:
 *
 *     pnpm gen:c-vectors
 *
 * The C implementation is conformant when it reproduces these results, which
 * are the same ones the TypeScript and Python implementations are held to.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "policybook/retry/retry.h"
#include "policybook/retry/exponential_full_jitter.h"
#include "policybook/rng.h"

#include "../pb_test.h"

/* smoke: a uniform draw under a doubling ceiling */
static void case_0(void)
{
    pb_rng rng;
    pb_retry_exponential_full_jitter_params params = PB_RETRY_EXPONENTIAL_FULL_JITTER_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 12u;
    policy = pb_retry_exponential_full_jitter.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 1u, &error), 6);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 2u, &error), 181);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 3u, &error), 377);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 4u, &error), 417);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 5u, &error), 1600);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 6u, &error), 789);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 7u, &error), 873);

    pb_retry_exponential_full_jitter.destroy(policy);
}

/* boundary: the ceiling caps, and a delay of zero is reachable */
static void case_1(void)
{
    pb_rng rng;
    pb_retry_exponential_full_jitter_params params = PB_RETRY_EXPONENTIAL_FULL_JITTER_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 3u);
    params.base_ms = 1u;
    params.cap_ms = 1u;
    params.max_attempts = 6u;
    policy = pb_retry_exponential_full_jitter.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 1u, &error), 0);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 2u, &error), 0);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 3u, &error), 1);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 4u, &error), 1);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 5u, &error), 0);

    pb_retry_exponential_full_jitter.destroy(policy);
}

/* distinguishing: the same attempt number gives different delays */
static void case_2(void)
{
    pb_rng rng;
    pb_retry_exponential_full_jitter_params params = PB_RETRY_EXPONENTIAL_FULL_JITTER_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 7u);
    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 12u;
    policy = pb_retry_exponential_full_jitter.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 3u, &error), 79);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 3u, &error), 13);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 3u, &error), 196);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 3u, &error), 361);

    pb_retry_exponential_full_jitter.destroy(policy);
}

/* tiebreak: give-up beats the draw, at maxAttempts and on a permanent failure */
static void case_3(void)
{
    pb_rng rng;
    pb_retry_exponential_full_jitter_params params = PB_RETRY_EXPONENTIAL_FULL_JITTER_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 3u;
    policy = pb_retry_exponential_full_jitter.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 3u, &error), PB_RETRY_GIVE_UP);
    error = (pb_retry_error){ 400, false, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 1u, &error), PB_RETRY_GIVE_UP);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(policy, 9u, &error), PB_RETRY_GIVE_UP);

    pb_retry_exponential_full_jitter.destroy(policy);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    return pb_test_summary("retry/exponential-full-jitter vectors");
}

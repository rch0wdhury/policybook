/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for retry/exponential, produced by scripts/gen-c-vectors.ts from
 * policies/retry/exponential/vectors.json. Regenerate with:
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
#include "policybook/retry/exponential.h"
#include "policybook/rng.h"

#include "../pb_test.h"

/* smoke: the delay doubles after every failure */
static void case_0(void)
{
    pb_rng rng;
    pb_retry_exponential_params params = PB_RETRY_EXPONENTIAL_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 12u;
    policy = pb_retry_exponential.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 1u, &error), 100);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 2u, &error), 200);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 3u, &error), 400);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 4u, &error), 800);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 5u, &error), 1600);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 6u, &error), 3200);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 7u, &error), 6400);

    pb_retry_exponential.destroy(policy);
}

/* boundary: the cap holds, and holds forever after */
static void case_1(void)
{
    pb_rng rng;
    pb_retry_exponential_params params = PB_RETRY_EXPONENTIAL_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 12u;
    policy = pb_retry_exponential.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 8u, &error), 10000);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 9u, &error), 10000);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 11u, &error), 10000);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 1000000u, &error), PB_RETRY_GIVE_UP);

    pb_retry_exponential.destroy(policy);
}

/* distinguishing: the sequence is a pure function of the attempt number */
static void case_2(void)
{
    pb_rng rng;
    pb_retry_exponential_params params = PB_RETRY_EXPONENTIAL_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 12u;
    policy = pb_retry_exponential.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 3u, &error), 400);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 3u, &error), 400);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 3u, &error), 400);

    pb_retry_exponential.destroy(policy);
}

/* tiebreak: give-up at maxAttempts, and on a permanent failure */
static void case_3(void)
{
    pb_rng rng;
    pb_retry_exponential_params params = PB_RETRY_EXPONENTIAL_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 3u;
    policy = pb_retry_exponential.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 1u, &error), 100);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 2u, &error), 200);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 3u, &error), PB_RETRY_GIVE_UP);
    error = (pb_retry_error){ 400, false, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 1u, &error), PB_RETRY_GIVE_UP);

    pb_retry_exponential.destroy(policy);
}

/* a cap below the base clamps the very first delay */
static void case_4(void)
{
    pb_rng rng;
    pb_retry_exponential_params params = PB_RETRY_EXPONENTIAL_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 5000u;
    params.cap_ms = 1000u;
    params.max_attempts = 4u;
    policy = pb_retry_exponential.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 1u, &error), 1000);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_exponential.next_delay(policy, 2u, &error), 1000);

    pb_retry_exponential.destroy(policy);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    case_4();
    return pb_test_summary("retry/exponential vectors");
}

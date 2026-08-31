/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for retry/constant, produced by scripts/gen-c-vectors.ts from
 * policies/retry/constant/vectors.json. Regenerate with:
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
#include "policybook/retry/constant.h"
#include "policybook/rng.h"

#include "../pb_test.h"

/* smoke: the same delay every time, then give up */
static void case_0(void)
{
    pb_rng rng;
    pb_retry_constant_params params = PB_RETRY_CONSTANT_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 100u;
    params.max_attempts = 4u;
    policy = pb_retry_constant.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_constant.next_delay(policy, 1u, &error), 100);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_constant.next_delay(policy, 2u, &error), 100);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_constant.next_delay(policy, 3u, &error), 100);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_constant.next_delay(policy, 4u, &error), PB_RETRY_GIVE_UP);

    pb_retry_constant.destroy(policy);
}

/* boundary: give-up is at maxAttempts, not one either side of it */
static void case_1(void)
{
    pb_rng rng;
    pb_retry_constant_params params = PB_RETRY_CONSTANT_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 250u;
    params.max_attempts = 1u;
    policy = pb_retry_constant.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_constant.next_delay(policy, 1u, &error), PB_RETRY_GIVE_UP);

    pb_retry_constant.destroy(policy);
}

/* distinguishing: the delay never changes, however long the outage runs */
static void case_2(void)
{
    pb_rng rng;
    pb_retry_constant_params params = PB_RETRY_CONSTANT_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 100u;
    params.max_attempts = 10u;
    policy = pb_retry_constant.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_constant.next_delay(policy, 1u, &error), 100);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_constant.next_delay(policy, 5u, &error), 100);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_constant.next_delay(policy, 9u, &error), 100);

    pb_retry_constant.destroy(policy);
}

/* tiebreak: a non-retryable failure gives up whatever the attempt number */
static void case_3(void)
{
    pb_rng rng;
    pb_retry_constant_params params = PB_RETRY_CONSTANT_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 100u;
    params.max_attempts = 4u;
    policy = pb_retry_constant.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 400, false, false, 0u };
    PB_CHECK_I64(pb_retry_constant.next_delay(policy, 1u, &error), PB_RETRY_GIVE_UP);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_constant.next_delay(policy, 1u, &error), 100);

    pb_retry_constant.destroy(policy);
}

/* a zero delay is a legitimate configuration */
static void case_4(void)
{
    pb_rng rng;
    pb_retry_constant_params params = PB_RETRY_CONSTANT_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 0u;
    params.max_attempts = 3u;
    policy = pb_retry_constant.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_constant.next_delay(policy, 1u, &error), 0);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_constant.next_delay(policy, 2u, &error), 0);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_constant.next_delay(policy, 3u, &error), PB_RETRY_GIVE_UP);

    pb_retry_constant.destroy(policy);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    case_4();
    return pb_test_summary("retry/constant vectors");
}

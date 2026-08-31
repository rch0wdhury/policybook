/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for retry/decorrelated-jitter, produced by scripts/gen-c-vectors.ts from
 * policies/retry/decorrelated-jitter/vectors.json. Regenerate with:
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
#include "policybook/retry/decorrelated_jitter.h"
#include "policybook/rng.h"

#include "../pb_test.h"

/* smoke: the walk starts at base and climbs from its own last step */
static void case_0(void)
{
    pb_rng rng;
    pb_retry_decorrelated_jitter_params params = PB_RETRY_DECORRELATED_JITTER_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 12u;
    policy = pb_retry_decorrelated_jitter.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    /* previousDelay() is not on the C vtable; step skipped */
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_decorrelated_jitter.next_delay(policy, 1u, &error), 191);
    /* previousDelay() is not on the C vtable; step skipped */
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_decorrelated_jitter.next_delay(policy, 2u, &error), 203);
    /* previousDelay() is not on the C vtable; step skipped */
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_decorrelated_jitter.next_delay(policy, 3u, &error), 501);
    /* previousDelay() is not on the C vtable; step skipped */
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_decorrelated_jitter.next_delay(policy, 4u, &error), 346);
    /* previousDelay() is not on the C vtable; step skipped */

    pb_retry_decorrelated_jitter.destroy(policy);
}

/* boundary: the walk stops at the cap and stays there */
static void case_1(void)
{
    pb_rng rng;
    pb_retry_decorrelated_jitter_params params = PB_RETRY_DECORRELATED_JITTER_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 5u);
    params.base_ms = 100u;
    params.cap_ms = 120u;
    params.max_attempts = 10u;
    policy = pb_retry_decorrelated_jitter.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_decorrelated_jitter.next_delay(policy, 1u, &error), 120);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_decorrelated_jitter.next_delay(policy, 2u, &error), 120);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_decorrelated_jitter.next_delay(policy, 3u, &error), 120);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_decorrelated_jitter.next_delay(policy, 4u, &error), 120);
    /* previousDelay() is not on the C vtable; step skipped */

    pb_retry_decorrelated_jitter.destroy(policy);
}

/* distinguishing: the attempt number does not determine the delay */
static void case_2(void)
{
    pb_rng rng;
    pb_retry_decorrelated_jitter_params params = PB_RETRY_DECORRELATED_JITTER_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 7u);
    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 12u;
    policy = pb_retry_decorrelated_jitter.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_decorrelated_jitter.next_delay(policy, 3u, &error), 267);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_decorrelated_jitter.next_delay(policy, 3u, &error), 410);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_decorrelated_jitter.next_delay(policy, 3u, &error), 417);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_decorrelated_jitter.next_delay(policy, 3u, &error), 390);
    /* previousDelay() is not on the C vtable; step skipped */

    pb_retry_decorrelated_jitter.destroy(policy);
}

/* tiebreak: give-up beats the draw, and leaves the walk untouched */
static void case_3(void)
{
    pb_rng rng;
    pb_retry_decorrelated_jitter_params params = PB_RETRY_DECORRELATED_JITTER_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 3u;
    policy = pb_retry_decorrelated_jitter.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_decorrelated_jitter.next_delay(policy, 3u, &error), PB_RETRY_GIVE_UP);
    error = (pb_retry_error){ 400, false, false, 0u };
    PB_CHECK_I64(pb_retry_decorrelated_jitter.next_delay(policy, 1u, &error), PB_RETRY_GIVE_UP);
    /* previousDelay() is not on the C vtable; step skipped */
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_decorrelated_jitter.next_delay(policy, 1u, &error), 191);

    pb_retry_decorrelated_jitter.destroy(policy);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    return pb_test_summary("retry/decorrelated-jitter vectors");
}

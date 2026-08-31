/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for retry/retry-after-aware, produced by scripts/gen-c-vectors.ts from
 * policies/retry/retry-after-aware/vectors.json. Regenerate with:
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
#include "policybook/retry/retry_after_aware.h"
#include "policybook/rng.h"

#include "../pb_test.h"

/* smoke: the server's own estimate is used verbatim */
static void case_0(void)
{
    pb_rng rng;
    pb_retry_retry_after_aware_params params = PB_RETRY_RETRY_AFTER_AWARE_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 12u;
    policy = pb_retry_retry_after_aware.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, true, 250u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 1u, &error), 250);
    error = (pb_retry_error){ 503, true, true, 1500u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 2u, &error), 1500);
    error = (pb_retry_error){ 503, true, true, 300u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 7u, &error), 300);

    pb_retry_retry_after_aware.destroy(policy);
}

/* boundary: the hint is clamped, and zero means come back now */
static void case_1(void)
{
    pb_rng rng;
    pb_retry_retry_after_aware_params params = PB_RETRY_RETRY_AFTER_AWARE_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 100u;
    params.cap_ms = 5000u;
    params.max_attempts = 12u;
    policy = pb_retry_retry_after_aware.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, true, 5000u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 1u, &error), 5000);
    error = (pb_retry_error){ 503, true, true, 5001u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 1u, &error), 5000);
    error = (pb_retry_error){ 503, true, true, 600000u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 1u, &error), 5000);
    error = (pb_retry_error){ 503, true, true, 0u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 1u, &error), 0);

    pb_retry_retry_after_aware.destroy(policy);
}

/* distinguishing: falls back to full jitter only when nothing was said */
static void case_2(void)
{
    pb_rng rng;
    pb_retry_retry_after_aware_params params = PB_RETRY_RETRY_AFTER_AWARE_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 5u);
    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 12u;
    policy = pb_retry_retry_after_aware.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, true, 777u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 3u, &error), 777);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 3u, &error), 375);
    error = (pb_retry_error){ 503, true, true, 777u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 3u, &error), 777);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 3u, &error), 9);

    pb_retry_retry_after_aware.destroy(policy);
}

/* tiebreak: the hinted path consumes no randomness */
static void case_3(void)
{
    pb_rng rng;
    pb_retry_retry_after_aware_params params = PB_RETRY_RETRY_AFTER_AWARE_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 5u);
    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 12u;
    policy = pb_retry_retry_after_aware.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, true, 10u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 1u, &error), 10);
    error = (pb_retry_error){ 503, true, true, 20u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 2u, &error), 20);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 3u, &error), 375);
    error = (pb_retry_error){ 503, true, true, 30u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 4u, &error), 30);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 3u, &error), 9);

    pb_retry_retry_after_aware.destroy(policy);
}

/* giving up ignores the hint entirely */
static void case_4(void)
{
    pb_rng rng;
    pb_retry_retry_after_aware_params params = PB_RETRY_RETRY_AFTER_AWARE_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 3u;
    policy = pb_retry_retry_after_aware.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, true, 100u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 3u, &error), PB_RETRY_GIVE_UP);
    error = (pb_retry_error){ 400, false, true, 50u };
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 1u, &error), PB_RETRY_GIVE_UP);

    pb_retry_retry_after_aware.destroy(policy);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    case_4();
    return pb_test_summary("retry/retry-after-aware vectors");
}

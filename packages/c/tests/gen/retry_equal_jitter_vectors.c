/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for retry/equal-jitter, produced by scripts/gen-c-vectors.ts from
 * policies/retry/equal-jitter/vectors.json. Regenerate with:
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
#include "policybook/retry/equal_jitter.h"
#include "policybook/rng.h"

#include "../pb_test.h"

/* smoke: half the ceiling fixed, half drawn */
static void case_0(void)
{
    pb_rng rng;
    pb_retry_equal_jitter_params params = PB_RETRY_EQUAL_JITTER_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 12u;
    policy = pb_retry_equal_jitter.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 1u, &error), 66);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 2u, &error), 141);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 3u, &error), 391);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 4u, &error), 667);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 5u, &error), 1247);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 6u, &error), 2036);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 7u, &error), 5553);

    pb_retry_equal_jitter.destroy(policy);
}

/* boundary: a ceiling of one halves to zero and the delay degenerates */
static void case_1(void)
{
    pb_rng rng;
    pb_retry_equal_jitter_params params = PB_RETRY_EQUAL_JITTER_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 3u);
    params.base_ms = 1u;
    params.cap_ms = 1u;
    params.max_attempts = 6u;
    policy = pb_retry_equal_jitter.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 1u, &error), 0);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 2u, &error), 0);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 3u, &error), 0);

    pb_retry_equal_jitter.destroy(policy);
}

/* distinguishing: never below half the ceiling, where full jitter can be zero */
static void case_2(void)
{
    pb_rng rng;
    pb_retry_equal_jitter_params params = PB_RETRY_EQUAL_JITTER_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 11u);
    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 12u;
    policy = pb_retry_equal_jitter.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 1u, &error), 79);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 1u, &error), 79);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 1u, &error), 82);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 1u, &error), 71);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 1u, &error), 94);

    pb_retry_equal_jitter.destroy(policy);
}

/* tiebreak: give-up beats the draw, at maxAttempts and on a permanent failure */
static void case_3(void)
{
    pb_rng rng;
    pb_retry_equal_jitter_params params = PB_RETRY_EQUAL_JITTER_PARAMS_DEFAULT;
    pb_retry *policy;
    pb_retry_error error;

    pb_rng_init(&rng, 1u);
    params.base_ms = 100u;
    params.cap_ms = 10000u;
    params.max_attempts = 3u;
    policy = pb_retry_equal_jitter.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 3u, &error), PB_RETRY_GIVE_UP);
    error = (pb_retry_error){ 400, false, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 1u, &error), PB_RETRY_GIVE_UP);
    error = (pb_retry_error){ 503, true, false, 0u };
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(policy, 9u, &error), PB_RETRY_GIVE_UP);

    pb_retry_equal_jitter.destroy(policy);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    return pb_test_summary("retry/equal-jitter vectors");
}

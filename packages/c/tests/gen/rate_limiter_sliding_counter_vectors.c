/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for rate-limiter/sliding-counter, produced by scripts/gen-c-vectors.ts from
 * policies/rate-limiter/sliding-counter/vectors.json. Regenerate with:
 *
 *     pnpm gen:c-vectors
 *
 * The C implementation is conformant when it reproduces these results, which
 * are the same ones the TypeScript and Python implementations are held to.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "policybook/rate_limiter/rate_limiter.h"
#include "policybook/rate_limiter/sliding_counter.h"
#include "policybook/rng.h"

#include "../pb_test.h"

/* smoke: the previous window's count decays instead of vanishing */
static void case_0(void)
{
    pb_rng rng;
    pb_ratelimiter_sliding_counter_params params = PB_RATELIMITER_SLIDING_COUNTER_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.limit = 5u;
    params.window_ms = 1000u;
    limiter = pb_ratelimiter_sliding_counter.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 1ULL, 1u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 1ULL, 1u, 10ULL) == true);
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 1ULL, 1u, 20ULL) == true);
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 1ULL, 1u, 30ULL) == true);
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 1ULL, 1u, 40ULL) == true);
    /* estimateOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 1ULL, 1u, 50ULL) == false);
    PB_CHECK_U64(pb_ratelimiter_sliding_counter.retry_after(limiter, 1ULL, 50ULL), 951ULL);
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 1ULL, 1u, 1000ULL) == false);
    /* estimateOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 1ULL, 1u, 1001ULL) == true);

    pb_ratelimiter_sliding_counter.destroy(limiter);
}

/* boundary: a gap of two windows clears both counts */
static void case_1(void)
{
    pb_rng rng;
    pb_ratelimiter_sliding_counter_params params = PB_RATELIMITER_SLIDING_COUNTER_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.limit = 5u;
    params.window_ms = 1000u;
    limiter = pb_ratelimiter_sliding_counter.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 7ULL, 5u, 900ULL) == true);
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 7ULL, 1u, 1000ULL) == false);
    /* estimateOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 7ULL, 1u, 1500ULL) == true);
    /* estimateOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 7ULL, 5u, 4000ULL) == true);

    pb_ratelimiter_sliding_counter.destroy(limiter);
}

/* distinguishing: a boundary burst is refused, then released gradually */
static void case_2(void)
{
    pb_rng rng;
    pb_ratelimiter_sliding_counter_params params = PB_RATELIMITER_SLIDING_COUNTER_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.limit = 5u;
    params.window_ms = 1000u;
    limiter = pb_ratelimiter_sliding_counter.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 3ULL, 5u, 999ULL) == true);
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 3ULL, 5u, 1000ULL) == false);
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 3ULL, 1u, 1000ULL) == false);
    /* estimateOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 3ULL, 3u, 1500ULL) == true);
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 3ULL, 1u, 1500ULL) == false);

    pb_ratelimiter_sliding_counter.destroy(limiter);
}

/* tiebreak: the weighting floors, and the floor decides the request */
static void case_3(void)
{
    pb_rng rng;
    pb_ratelimiter_sliding_counter_params params = PB_RATELIMITER_SLIDING_COUNTER_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.limit = 5u;
    params.window_ms = 1000u;
    limiter = pb_ratelimiter_sliding_counter.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 2ULL, 5u, 0ULL) == true);
    /* estimateOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 2ULL, 1u, 1001ULL) == true);
    /* estimateOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 2ULL, 1u, 1001ULL) == false);
    PB_CHECK_U64(pb_ratelimiter_sliding_counter.retry_after(limiter, 2ULL, 1001ULL), 200ULL);
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 2ULL, 1u, 1201ULL) == true);

    pb_ratelimiter_sliding_counter.destroy(limiter);
}

/* keys are independent, and each is remembered */
static void case_4(void)
{
    pb_rng rng;
    pb_ratelimiter_sliding_counter_params params = PB_RATELIMITER_SLIDING_COUNTER_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.limit = 5u;
    params.window_ms = 1000u;
    limiter = pb_ratelimiter_sliding_counter.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 1ULL, 5u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 1ULL, 1u, 0ULL) == false);
    PB_CHECK(pb_ratelimiter_sliding_counter.allow(limiter, 2ULL, 5u, 0ULL) == true);
    PB_CHECK_U64((uint64_t)pb_ratelimiter_sliding_counter.state_size(limiter), 2ULL);
    PB_CHECK_U64(pb_ratelimiter_sliding_counter.retry_after(limiter, 99ULL, 0ULL), 0ULL);
    PB_CHECK_U64((uint64_t)pb_ratelimiter_sliding_counter.state_size(limiter), 2ULL);

    pb_ratelimiter_sliding_counter.destroy(limiter);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    case_4();
    return pb_test_summary("rate-limiter/sliding-counter vectors");
}

/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for rate-limiter/sliding-log, produced by scripts/gen-c-vectors.ts from
 * policies/rate-limiter/sliding-log/vectors.json. Regenerate with:
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
#include "policybook/rate_limiter/sliding_log.h"
#include "policybook/rng.h"

#include "../pb_test.h"

/* smoke: the window slides with the clock, not with a fixed edge */
static void case_0(void)
{
    pb_rng rng;
    pb_ratelimiter_sliding_log_params params = PB_RATELIMITER_SLIDING_LOG_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.limit = 5u;
    params.window_ms = 1000u;
    limiter = pb_ratelimiter_sliding_log.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 1ULL, 1u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 1ULL, 1u, 10ULL) == true);
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 1ULL, 1u, 20ULL) == true);
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 1ULL, 1u, 30ULL) == true);
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 1ULL, 1u, 40ULL) == true);
    /* countOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 1ULL, 1u, 50ULL) == false);
    PB_CHECK_U64(pb_ratelimiter_sliding_log.retry_after(limiter, 1ULL, 50ULL), 950ULL);
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 1ULL, 1u, 1000ULL) == true);
    /* countOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 1ULL, 1u, 1005ULL) == false);
    PB_CHECK_U64(pb_ratelimiter_sliding_log.retry_after(limiter, 1ULL, 1005ULL), 5ULL);
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 1ULL, 1u, 1010ULL) == true);

    pb_ratelimiter_sliding_log.destroy(limiter);
}

/* boundary: an entry leaves the window exactly windowMs after it arrived */
static void case_1(void)
{
    pb_rng rng;
    pb_ratelimiter_sliding_log_params params = PB_RATELIMITER_SLIDING_LOG_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.limit = 5u;
    params.window_ms = 1000u;
    limiter = pb_ratelimiter_sliding_log.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 4ULL, 1u, 0ULL) == true);
    /* countOf() is not on the C vtable; step skipped */
    /* countOf() is not on the C vtable; step skipped */
    PB_CHECK_U64((uint64_t)pb_ratelimiter_sliding_log.state_size(limiter), 1ULL);

    pb_ratelimiter_sliding_log.destroy(limiter);
}

/* distinguishing: a boundary burst is refused, unlike a fixed window */
static void case_2(void)
{
    pb_rng rng;
    pb_ratelimiter_sliding_log_params params = PB_RATELIMITER_SLIDING_LOG_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.limit = 5u;
    params.window_ms = 1000u;
    limiter = pb_ratelimiter_sliding_log.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 3ULL, 5u, 999ULL) == true);
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 3ULL, 1u, 999ULL) == false);
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 3ULL, 5u, 1000ULL) == false);
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 3ULL, 1u, 1000ULL) == false);
    /* countOf() is not on the C vtable; step skipped */
    PB_CHECK_U64(pb_ratelimiter_sliding_log.retry_after(limiter, 3ULL, 1000ULL), 999ULL);
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 3ULL, 5u, 1999ULL) == true);

    pb_ratelimiter_sliding_log.destroy(limiter);
}

/* tiebreak: a request costing exactly the free space is admitted */
static void case_3(void)
{
    pb_rng rng;
    pb_ratelimiter_sliding_log_params params = PB_RATELIMITER_SLIDING_LOG_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.limit = 5u;
    params.window_ms = 1000u;
    limiter = pb_ratelimiter_sliding_log.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 2ULL, 3u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 2ULL, 3u, 100ULL) == false);
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 2ULL, 2u, 100ULL) == true);
    /* countOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 2ULL, 1u, 100ULL) == false);
    /* countOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 2ULL, 3u, 1000ULL) == true);
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 2ULL, 1u, 1000ULL) == false);

    pb_ratelimiter_sliding_log.destroy(limiter);
}

/* the ring wraps: entries reused after a full cycle stay in order */
static void case_4(void)
{
    pb_rng rng;
    pb_ratelimiter_sliding_log_params params = PB_RATELIMITER_SLIDING_LOG_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.limit = 5u;
    params.window_ms = 1000u;
    limiter = pb_ratelimiter_sliding_log.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 8ULL, 5u, 0ULL) == true);
    /* countOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 8ULL, 3u, 1000ULL) == true);
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 8ULL, 2u, 1500ULL) == true);
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 8ULL, 1u, 1500ULL) == false);
    PB_CHECK_U64(pb_ratelimiter_sliding_log.retry_after(limiter, 8ULL, 1500ULL), 500ULL);
    /* countOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_sliding_log.allow(limiter, 8ULL, 3u, 2000ULL) == true);

    pb_ratelimiter_sliding_log.destroy(limiter);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    case_4();
    return pb_test_summary("rate-limiter/sliding-log vectors");
}

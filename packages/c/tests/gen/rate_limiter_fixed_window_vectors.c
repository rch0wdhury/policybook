/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for rate-limiter/fixed-window, produced by scripts/gen-c-vectors.ts from
 * policies/rate-limiter/fixed-window/vectors.json. Regenerate with:
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
#include "policybook/rate_limiter/fixed_window.h"
#include "policybook/rng.h"

#include "../pb_test.h"

/* smoke: the first `limit` requests pass and the next is refused */
static void case_0(void)
{
    pb_rng rng;
    pb_ratelimiter_fixed_window_params params = PB_RATELIMITER_FIXED_WINDOW_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.limit = 5u;
    params.window_ms = 1000u;
    limiter = pb_ratelimiter_fixed_window.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 1ULL, 1u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 1ULL, 1u, 10ULL) == true);
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 1ULL, 1u, 20ULL) == true);
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 1ULL, 1u, 30ULL) == true);
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 1ULL, 1u, 40ULL) == true);
    /* countOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 1ULL, 1u, 50ULL) == false);
    PB_CHECK_U64(pb_ratelimiter_fixed_window.retry_after(limiter, 1ULL, 50ULL), 950ULL);
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 1ULL, 1u, 1000ULL) == true);

    pb_ratelimiter_fixed_window.destroy(limiter);
}

/* boundary: windows are aligned to the epoch, not to the first request */
static void case_1(void)
{
    pb_rng rng;
    pb_ratelimiter_fixed_window_params params = PB_RATELIMITER_FIXED_WINDOW_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.limit = 5u;
    params.window_ms = 1000u;
    limiter = pb_ratelimiter_fixed_window.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 7ULL, 1u, 900ULL) == true);
    /* countOf() is not on the C vtable; step skipped */
    /* countOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 7ULL, 4u, 950ULL) == true);
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 7ULL, 1u, 960ULL) == false);
    PB_CHECK_U64(pb_ratelimiter_fixed_window.retry_after(limiter, 7ULL, 960ULL), 40ULL);
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 7ULL, 5u, 1000ULL) == true);

    pb_ratelimiter_fixed_window.destroy(limiter);
}

/* distinguishing: twice the limit passes across a window boundary */
static void case_2(void)
{
    pb_rng rng;
    pb_ratelimiter_fixed_window_params params = PB_RATELIMITER_FIXED_WINDOW_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.limit = 5u;
    params.window_ms = 1000u;
    limiter = pb_ratelimiter_fixed_window.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 3ULL, 5u, 999ULL) == true);
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 3ULL, 1u, 999ULL) == false);
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 3ULL, 5u, 1000ULL) == true);
    /* countOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 3ULL, 1u, 1000ULL) == false);

    pb_ratelimiter_fixed_window.destroy(limiter);
}

/* tiebreak: a request costing exactly the remaining budget is admitted */
static void case_3(void)
{
    pb_rng rng;
    pb_ratelimiter_fixed_window_params params = PB_RATELIMITER_FIXED_WINDOW_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.limit = 5u;
    params.window_ms = 1000u;
    limiter = pb_ratelimiter_fixed_window.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 2ULL, 3u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 2ULL, 3u, 100ULL) == false);
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 2ULL, 2u, 100ULL) == true);
    /* countOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 2ULL, 1u, 100ULL) == false);
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 9ULL, 6u, 0ULL) == false);
    /* countOf() is not on the C vtable; step skipped */

    pb_ratelimiter_fixed_window.destroy(limiter);
}

/* keys are independent, and each is remembered */
static void case_4(void)
{
    pb_rng rng;
    pb_ratelimiter_fixed_window_params params = PB_RATELIMITER_FIXED_WINDOW_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.limit = 5u;
    params.window_ms = 1000u;
    limiter = pb_ratelimiter_fixed_window.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 1ULL, 5u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 1ULL, 1u, 0ULL) == false);
    PB_CHECK(pb_ratelimiter_fixed_window.allow(limiter, 2ULL, 5u, 0ULL) == true);
    PB_CHECK_U64((uint64_t)pb_ratelimiter_fixed_window.state_size(limiter), 2ULL);
    PB_CHECK_U64(pb_ratelimiter_fixed_window.retry_after(limiter, 99ULL, 0ULL), 0ULL);
    PB_CHECK_U64((uint64_t)pb_ratelimiter_fixed_window.state_size(limiter), 2ULL);

    pb_ratelimiter_fixed_window.destroy(limiter);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    case_4();
    return pb_test_summary("rate-limiter/fixed-window vectors");
}

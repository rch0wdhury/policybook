/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for rate-limiter/dual-bucket, produced by scripts/gen-c-vectors.ts from
 * policies/rate-limiter/dual-bucket/vectors.json. Regenerate with:
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
#include "policybook/rate_limiter/dual_bucket.h"
#include "policybook/rng.h"

#include "../pb_test.h"

/* smoke: every call charges both dimensions */
static void case_0(void)
{
    pb_rng rng;
    pb_ratelimiter_dual_bucket_params params = PB_RATELIMITER_DUAL_BUCKET_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.requests_per_min = 3u;
    params.tokens_per_min = 1000u;
    limiter = pb_ratelimiter_dual_bucket.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    /* requestsOf() is not on the C vtable; step skipped */
    /* tokensOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 1ULL, 100u, 0ULL) == true);
    /* requestsOf() is not on the C vtable; step skipped */
    /* tokensOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 1ULL, 100u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 1ULL, 100u, 0ULL) == true);
    /* requestsOf() is not on the C vtable; step skipped */
    /* tokensOf() is not on the C vtable; step skipped */

    pb_ratelimiter_dual_bucket.destroy(limiter);
}

/* distinguishing: either dimension refuses on its own */
static void case_1(void)
{
    pb_rng rng;
    pb_ratelimiter_dual_bucket_params params = PB_RATELIMITER_DUAL_BUCKET_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.requests_per_min = 3u;
    params.tokens_per_min = 1000u;
    limiter = pb_ratelimiter_dual_bucket.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 1ULL, 100u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 1ULL, 100u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 1ULL, 100u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 1ULL, 100u, 0ULL) == false);
    /* requestsOf() is not on the C vtable; step skipped */
    /* tokensOf() is not on the C vtable; step skipped */
    PB_CHECK_U64(pb_ratelimiter_dual_bucket.retry_after(limiter, 1ULL, 0ULL), 20000ULL);
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 2ULL, 1000u, 0ULL) == true);
    /* requestsOf() is not on the C vtable; step skipped */
    /* tokensOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 2ULL, 1u, 0ULL) == false);
    PB_CHECK_U64(pb_ratelimiter_dual_bucket.retry_after(limiter, 2ULL, 0ULL), 60ULL);
    /* tokensOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 2ULL, 1u, 60ULL) == true);

    pb_ratelimiter_dual_bucket.destroy(limiter);
}

/* tiebreak: a refused call charges nothing, on either dimension */
static void case_2(void)
{
    pb_rng rng;
    pb_ratelimiter_dual_bucket_params params = PB_RATELIMITER_DUAL_BUCKET_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.requests_per_min = 3u;
    params.tokens_per_min = 1000u;
    limiter = pb_ratelimiter_dual_bucket.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 2ULL, 1000u, 0ULL) == true);
    /* requestsOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 2ULL, 1u, 0ULL) == false);
    /* requestsOf() is not on the C vtable; step skipped */
    /* tokensOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 6ULL, 600u, 0ULL) == true);
    /* tokensOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 6ULL, 500u, 0ULL) == false);
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 6ULL, 400u, 0ULL) == true);
    /* tokensOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 7ULL, 1001u, 0ULL) == false);
    /* requestsOf() is not on the C vtable; step skipped */
    /* tokensOf() is not on the C vtable; step skipped */

    pb_ratelimiter_dual_bucket.destroy(limiter);
}

/* boundary: both ceilings saturate, and idling past a minute adds nothing */
static void case_3(void)
{
    pb_rng rng;
    pb_ratelimiter_dual_bucket_params params = PB_RATELIMITER_DUAL_BUCKET_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.requests_per_min = 3u;
    params.tokens_per_min = 1000u;
    limiter = pb_ratelimiter_dual_bucket.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 3ULL, 1000u, 0ULL) == true);
    /* requestsOf() is not on the C vtable; step skipped */
    /* tokensOf() is not on the C vtable; step skipped */
    /* requestsOf() is not on the C vtable; step skipped */
    /* tokensOf() is not on the C vtable; step skipped */
    /* requestsOf() is not on the C vtable; step skipped */
    /* tokensOf() is not on the C vtable; step skipped */
    PB_CHECK_U64(pb_ratelimiter_dual_bucket.retry_after(limiter, 3ULL, 2592000000ULL), 0ULL);
    PB_CHECK_U64((uint64_t)pb_ratelimiter_dual_bucket.state_size(limiter), 1ULL);

    pb_ratelimiter_dual_bucket.destroy(limiter);
}

/* the two dimensions refill independently */
static void case_4(void)
{
    pb_rng rng;
    pb_ratelimiter_dual_bucket_params params = PB_RATELIMITER_DUAL_BUCKET_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.requests_per_min = 3u;
    params.tokens_per_min = 1000u;
    limiter = pb_ratelimiter_dual_bucket.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 4ULL, 333u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 4ULL, 333u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 4ULL, 334u, 0ULL) == true);
    /* requestsOf() is not on the C vtable; step skipped */
    /* tokensOf() is not on the C vtable; step skipped */
    /* tokensOf() is not on the C vtable; step skipped */
    /* requestsOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 4ULL, 1u, 10000ULL) == false);
    PB_CHECK_U64(pb_ratelimiter_dual_bucket.retry_after(limiter, 4ULL, 10000ULL), 10000ULL);
    /* requestsOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 4ULL, 1u, 20000ULL) == true);

    pb_ratelimiter_dual_bucket.destroy(limiter);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    case_4();
    return pb_test_summary("rate-limiter/dual-bucket vectors");
}

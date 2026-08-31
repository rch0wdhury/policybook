/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for rate-limiter/leaky-bucket, produced by scripts/gen-c-vectors.ts from
 * policies/rate-limiter/leaky-bucket/vectors.json. Regenerate with:
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
#include "policybook/rate_limiter/leaky_bucket.h"
#include "policybook/rng.h"

#include "../pb_test.h"

/* smoke: fill the bucket, then one unit of room every 10 ms */
static void case_0(void)
{
    pb_rng rng;
    pb_ratelimiter_leaky_bucket_params params = PB_RATELIMITER_LEAKY_BUCKET_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.rate_per_sec = 100u;
    params.capacity = 5u;
    limiter = pb_ratelimiter_leaky_bucket.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    /* levelOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 0ULL) == true);
    /* levelOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 0ULL) == false);
    PB_CHECK_U64(pb_ratelimiter_leaky_bucket.retry_after(limiter, 1ULL, 0ULL), 10ULL);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 9ULL) == false);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 10ULL) == true);
    /* levelOf() is not on the C vtable; step skipped */

    pb_ratelimiter_leaky_bucket.destroy(limiter);
}

/* boundary: the bucket empties and stays empty, however long it idles */
static void case_1(void)
{
    pb_rng rng;
    pb_ratelimiter_leaky_bucket_params params = PB_RATELIMITER_LEAKY_BUCKET_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.rate_per_sec = 100u;
    params.capacity = 5u;
    limiter = pb_ratelimiter_leaky_bucket.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 5u, 0ULL) == true);
    /* levelOf() is not on the C vtable; step skipped */
    /* levelOf() is not on the C vtable; step skipped */
    /* levelOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 5u, 2592000000ULL) == true);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 2592000000ULL) == false);

    pb_ratelimiter_leaky_bucket.destroy(limiter);
}

/* distinguishing: at its default capacity it smooths to exact spacing */
static void case_2(void)
{
    pb_rng rng;
    pb_ratelimiter_leaky_bucket_params params = PB_RATELIMITER_LEAKY_BUCKET_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.rate_per_sec = 100u;
    params.capacity = 1u;
    limiter = pb_ratelimiter_leaky_bucket.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 0ULL) == false);
    PB_CHECK_U64(pb_ratelimiter_leaky_bucket.retry_after(limiter, 1ULL, 0ULL), 10ULL);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 9ULL) == false);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 10ULL) == true);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 10ULL) == false);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 19ULL) == false);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 20ULL) == true);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 2u, 10000ULL) == false);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 10000ULL) == true);

    pb_ratelimiter_leaky_bucket.destroy(limiter);
}

/* tiebreak: a request filling exactly the remaining room is admitted */
static void case_3(void)
{
    pb_rng rng;
    pb_ratelimiter_leaky_bucket_params params = PB_RATELIMITER_LEAKY_BUCKET_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.rate_per_sec = 100u;
    params.capacity = 5u;
    limiter = pb_ratelimiter_leaky_bucket.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 3ULL, 3u, 0ULL) == true);
    /* levelOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 3ULL, 3u, 0ULL) == false);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 3ULL, 2u, 0ULL) == true);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 3ULL, 1u, 0ULL) == false);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 4ULL, 6u, 0ULL) == false);
    /* levelOf() is not on the C vtable; step skipped */

    pb_ratelimiter_leaky_bucket.destroy(limiter);
}

/* the fractional carry survives between drains */
static void case_4(void)
{
    pb_rng rng;
    pb_ratelimiter_leaky_bucket_params params = PB_RATELIMITER_LEAKY_BUCKET_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.rate_per_sec = 3u;
    params.capacity = 2u;
    limiter = pb_ratelimiter_leaky_bucket.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 2u, 0ULL) == true);
    /* levelOf() is not on the C vtable; step skipped */
    /* levelOf() is not on the C vtable; step skipped */
    /* levelOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 334ULL) == true);
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 1ULL, 1u, 334ULL) == false);
    PB_CHECK_U64(pb_ratelimiter_leaky_bucket.retry_after(limiter, 1ULL, 334ULL), 333ULL);

    pb_ratelimiter_leaky_bucket.destroy(limiter);
}

/* equivalence: the mirror of the token bucket's own case */
static void case_5(void)
{
    pb_rng rng;
    pb_ratelimiter_leaky_bucket_params params = PB_RATELIMITER_LEAKY_BUCKET_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;

    pb_rng_init(&rng, 1u);
    params.rate_per_sec = 100u;
    params.capacity = 5u;
    limiter = pb_ratelimiter_leaky_bucket.create(&params, NULL, &rng);
    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    /* levelOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 7ULL, 2u, 0ULL) == true);
    /* levelOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 7ULL, 3u, 0ULL) == true);
    /* levelOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 7ULL, 1u, 0ULL) == false);
    PB_CHECK_U64(pb_ratelimiter_leaky_bucket.retry_after(limiter, 7ULL, 0ULL), 10ULL);
    /* levelOf() is not on the C vtable; step skipped */
    PB_CHECK(pb_ratelimiter_leaky_bucket.allow(limiter, 7ULL, 3u, 30ULL) == true);
    /* levelOf() is not on the C vtable; step skipped */
    PB_CHECK_U64((uint64_t)pb_ratelimiter_leaky_bucket.state_size(limiter), 1ULL);

    pb_ratelimiter_leaky_bucket.destroy(limiter);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    case_4();
    case_5();
    return pb_test_summary("rate-limiter/leaky-bucket vectors");
}

/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for cache/w-tinylfu, produced by scripts/gen-c-vectors.ts from
 * policies/cache/w-tinylfu/vectors.json. Regenerate with:
 *
 *     pnpm gen:c-vectors
 *
 * The C implementation is conformant when it reproduces these results, which
 * are the same ones the TypeScript and Python implementations are held to.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "policybook/cache/cache.h"
#include "policybook/cache/w_tinylfu.h"
#include "policybook/hash.h"
#include "policybook/rng.h"

#include "../pb_test.h"

/* smoke: new keys land in the window and drain into probation */
static void case_0(void)
{
    pb_rng rng;
    pb_cache_w_tinylfu_params params = PB_CACHE_W_TINYLFU_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 4u;
    cache = pb_cache_w_tinylfu.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_w_tinylfu.on_access(cache, 1ULL, false, NULL);
    /* segmentOf() is not on the C vtable; step skipped */
    pb_cache_w_tinylfu.on_access(cache, 2ULL, false, NULL);
    /* segmentOf() is not on the C vtable; step skipped */
    /* segmentOf() is not on the C vtable; step skipped */
    pb_cache_w_tinylfu.on_access(cache, 3ULL, false, NULL);
    pb_cache_w_tinylfu.on_access(cache, 4ULL, false, NULL);
    /* size() is not on the C vtable; step skipped */
    /* segmentOf() is not on the C vtable; step skipped */

    pb_cache_w_tinylfu.destroy(cache);
}

/* boundary: a capacity of two still separates window from main */
static void case_1(void)
{
    pb_rng rng;
    pb_cache_w_tinylfu_params params = PB_CACHE_W_TINYLFU_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 2u;
    cache = pb_cache_w_tinylfu.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_w_tinylfu.on_access(cache, 1ULL, false, NULL);
    pb_cache_w_tinylfu.on_access(cache, 2ULL, false, NULL);
    /* segmentOf() is not on the C vtable; step skipped */
    /* segmentOf() is not on the C vtable; step skipped */
    pb_cache_w_tinylfu.on_access(cache, 3ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 2ULL);
    /* size() is not on the C vtable; step skipped */

    pb_cache_w_tinylfu.destroy(cache);
}

/* distinguishing: an unpopular newcomer is refused admission, unlike LRU */
static void case_2(void)
{
    pb_rng rng;
    pb_cache_w_tinylfu_params params = PB_CACHE_W_TINYLFU_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 4u;
    cache = pb_cache_w_tinylfu.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_w_tinylfu.on_access(cache, 1ULL, false, NULL);
    pb_cache_w_tinylfu.on_access(cache, 2ULL, false, NULL);
    pb_cache_w_tinylfu.on_access(cache, 3ULL, false, NULL);
    pb_cache_w_tinylfu.on_access(cache, 4ULL, false, NULL);
    pb_cache_w_tinylfu.on_access(cache, 5ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 4ULL);
    /* segmentOf() is not on the C vtable; step skipped */
    /* segmentOf() is not on the C vtable; step skipped */
    pb_cache_w_tinylfu.on_access(cache, 4ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 5ULL);
    /* frequencyOf() is not on the C vtable; step skipped */
    pb_cache_w_tinylfu.on_access(cache, 6ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 1ULL);
    /* segmentOf() is not on the C vtable; step skipped */

    pb_cache_w_tinylfu.destroy(cache);
}

/* tiebreak: on equal frequency the incumbent keeps its place */
static void case_3(void)
{
    pb_rng rng;
    pb_cache_w_tinylfu_params params = PB_CACHE_W_TINYLFU_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 4u;
    cache = pb_cache_w_tinylfu.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_w_tinylfu.on_access(cache, 1ULL, false, NULL);
    pb_cache_w_tinylfu.on_access(cache, 2ULL, false, NULL);
    pb_cache_w_tinylfu.on_access(cache, 3ULL, false, NULL);
    pb_cache_w_tinylfu.on_access(cache, 4ULL, false, NULL);
    /* frequencyOf() is not on the C vtable; step skipped */
    /* frequencyOf() is not on the C vtable; step skipped */
    pb_cache_w_tinylfu.on_access(cache, 5ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 4ULL);

    pb_cache_w_tinylfu.destroy(cache);
}

/* the doorkeeper absorbs a key's first appearance */
static void case_4(void)
{
    pb_rng rng;
    pb_cache_w_tinylfu_params params = PB_CACHE_W_TINYLFU_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 4u;
    cache = pb_cache_w_tinylfu.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    /* frequencyOf() is not on the C vtable; step skipped */
    pb_cache_w_tinylfu.on_access(cache, 7ULL, false, NULL);
    /* frequencyOf() is not on the C vtable; step skipped */
    pb_cache_w_tinylfu.on_access(cache, 7ULL, true, NULL);
    /* frequencyOf() is not on the C vtable; step skipped */
    pb_cache_w_tinylfu.on_access(cache, 7ULL, true, NULL);
    /* frequencyOf() is not on the C vtable; step skipped */
    /* frequencyOf() is not on the C vtable; step skipped */

    pb_cache_w_tinylfu.destroy(cache);
}

/* sketch collisions: readings and admissions both follow the hash */
static void case_5(void)
{
    pb_rng rng;
    pb_cache_w_tinylfu_params params = PB_CACHE_W_TINYLFU_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 4u;
    cache = pb_cache_w_tinylfu.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_w_tinylfu.on_access(cache, 10ULL, false, NULL);
    pb_cache_w_tinylfu.on_access(cache, 11ULL, false, NULL);
    pb_cache_w_tinylfu.on_access(cache, 12ULL, false, NULL);
    pb_cache_w_tinylfu.on_access(cache, 13ULL, false, NULL);
    pb_cache_w_tinylfu.on_access(cache, 14ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 13ULL);
    pb_cache_w_tinylfu.on_access(cache, 15ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 14ULL);
    pb_cache_w_tinylfu.on_access(cache, 16ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 15ULL);
    pb_cache_w_tinylfu.on_access(cache, 17ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 16ULL);
    pb_cache_w_tinylfu.on_access(cache, 18ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 17ULL);
    pb_cache_w_tinylfu.on_access(cache, 19ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 10ULL);
    pb_cache_w_tinylfu.on_access(cache, 20ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 19ULL);
    pb_cache_w_tinylfu.on_access(cache, 21ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 11ULL);
    pb_cache_w_tinylfu.on_access(cache, 10ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 21ULL);
    pb_cache_w_tinylfu.on_access(cache, 11ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 12ULL);
    pb_cache_w_tinylfu.on_access(cache, 12ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 11ULL);
    pb_cache_w_tinylfu.on_access(cache, 13ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 12ULL);
    pb_cache_w_tinylfu.on_access(cache, 14ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 13ULL);
    pb_cache_w_tinylfu.on_access(cache, 15ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 14ULL);
    pb_cache_w_tinylfu.on_access(cache, 16ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 15ULL);
    pb_cache_w_tinylfu.on_access(cache, 17ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 16ULL);
    pb_cache_w_tinylfu.on_access(cache, 18ULL, true, NULL);
    pb_cache_w_tinylfu.on_access(cache, 19ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 17ULL);
    pb_cache_w_tinylfu.on_access(cache, 20ULL, true, NULL);
    pb_cache_w_tinylfu.on_access(cache, 21ULL, false, NULL);
    PB_CHECK_U64(pb_cache_w_tinylfu.evict(cache), 19ULL);
    /* frequencyOf() is not on the C vtable; step skipped */
    /* frequencyOf() is not on the C vtable; step skipped */
    /* frequencyOf() is not on the C vtable; step skipped */
    /* frequencyOf() is not on the C vtable; step skipped */
    /* frequencyOf() is not on the C vtable; step skipped */
    /* frequencyOf() is not on the C vtable; step skipped */
    /* frequencyOf() is not on the C vtable; step skipped */
    /* frequencyOf() is not on the C vtable; step skipped */
    /* frequencyOf() is not on the C vtable; step skipped */
    /* frequencyOf() is not on the C vtable; step skipped */
    /* frequencyOf() is not on the C vtable; step skipped */
    /* frequencyOf() is not on the C vtable; step skipped */
    /* frequencyOf() is not on the C vtable; step skipped */

    pb_cache_w_tinylfu.destroy(cache);
}

/* a second hit in the main cache promotes to protected */
static void case_6(void)
{
    pb_rng rng;
    pb_cache_w_tinylfu_params params = PB_CACHE_W_TINYLFU_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 4u;
    cache = pb_cache_w_tinylfu.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_w_tinylfu.on_access(cache, 1ULL, false, NULL);
    pb_cache_w_tinylfu.on_access(cache, 2ULL, false, NULL);
    pb_cache_w_tinylfu.on_access(cache, 3ULL, false, NULL);
    pb_cache_w_tinylfu.on_access(cache, 4ULL, false, NULL);
    /* segmentOf() is not on the C vtable; step skipped */
    pb_cache_w_tinylfu.on_access(cache, 1ULL, true, NULL);
    /* segmentOf() is not on the C vtable; step skipped */
    pb_cache_w_tinylfu.on_access(cache, 2ULL, true, NULL);
    /* segmentOf() is not on the C vtable; step skipped */
    pb_cache_w_tinylfu.on_access(cache, 3ULL, true, NULL);
    /* segmentOf() is not on the C vtable; step skipped */
    /* segmentOf() is not on the C vtable; step skipped */

    pb_cache_w_tinylfu.destroy(cache);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    case_4();
    case_5();
    case_6();
    return pb_test_summary("cache/w-tinylfu vectors");
}

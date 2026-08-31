/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for cache/lfu, produced by scripts/gen-c-vectors.ts from
 * policies/cache/lfu/vectors.json. Regenerate with:
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
#include "policybook/cache/lfu.h"
#include "policybook/hash.h"
#include "policybook/rng.h"

#include "../pb_test.h"

/* smoke: the least used key leaves first */
static void case_0(void)
{
    pb_rng rng;
    pb_cache_lfu_params params = PB_CACHE_LFU_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 3u;
    cache = pb_cache_lfu.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_lfu.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_lfu.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_lfu.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_lfu.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_lfu.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_lfu.on_access(cache, 12638190499090526629ULL, true, NULL);
    /* frequencyOf() is not on the C vtable; step skipped */
    /* frequencyOf() is not on the C vtable; step skipped */
    pb_cache_lfu.on_access(cache, 12638183902020757363ULL, false, NULL);
    PB_CHECK_U64(pb_cache_lfu.evict(cache), 12638189399578898418ULL);

    pb_cache_lfu.destroy(cache);
}

/* boundary: a capacity of one holds only the newest key */
static void case_1(void)
{
    pb_rng rng;
    pb_cache_lfu_params params = PB_CACHE_LFU_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 1u;
    cache = pb_cache_lfu.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_lfu.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_lfu.on_access(cache, 12638190499090526629ULL, false, NULL);
    PB_CHECK_U64(pb_cache_lfu.evict(cache), 12638187200555641996ULL);
    /* size() is not on the C vtable; step skipped */

    pb_cache_lfu.destroy(cache);
}

/* distinguishing: a frequently used key survives going cold, unlike LRU */
static void case_2(void)
{
    pb_rng rng;
    pb_cache_lfu_params params = PB_CACHE_LFU_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 3u;
    cache = pb_cache_lfu.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_lfu.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_lfu.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_lfu.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_lfu.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_lfu.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_lfu.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_lfu.on_access(cache, 12638183902020757363ULL, false, NULL);
    PB_CHECK_U64(pb_cache_lfu.evict(cache), 12638190499090526629ULL);

    pb_cache_lfu.destroy(cache);
}

/* tiebreak: among equal counts, the one that reached it first goes */
static void case_3(void)
{
    pb_rng rng;
    pb_cache_lfu_params params = PB_CACHE_LFU_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 4u;
    cache = pb_cache_lfu.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_lfu.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_lfu.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_lfu.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_lfu.on_access(cache, 12638189399578898418ULL, true, NULL);
    pb_cache_lfu.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_lfu.on_access(cache, 12638183902020757363ULL, false, NULL);
    pb_cache_lfu.on_access(cache, 12638182802509129152ULL, false, NULL);
    PB_CHECK_U64(pb_cache_lfu.evict(cache), 12638190499090526629ULL);
    PB_CHECK_U64(pb_cache_lfu.evict(cache), 12638183902020757363ULL);
    PB_CHECK_U64(pb_cache_lfu.evict(cache), 12638182802509129152ULL);
    PB_CHECK_U64(pb_cache_lfu.evict(cache), 12638189399578898418ULL);
    PB_CHECK_U64(pb_cache_lfu.evict(cache), 12638187200555641996ULL);

    pb_cache_lfu.destroy(cache);
}

/* a scan does not displace the working set */
static void case_4(void)
{
    pb_rng rng;
    pb_cache_lfu_params params = PB_CACHE_LFU_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 3u;
    cache = pb_cache_lfu.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_lfu.on_access(cache, 3701717109319258324ULL, false, NULL);
    pb_cache_lfu.on_access(cache, 3701717109319258324ULL, true, NULL);
    pb_cache_lfu.on_access(cache, 3701717109319258324ULL, true, NULL);
    pb_cache_lfu.on_access(cache, 637539755847373129ULL, false, NULL);
    pb_cache_lfu.on_access(cache, 637536457312488496ULL, false, NULL);
    pb_cache_lfu.on_access(cache, 637537556824116707ULL, false, NULL);
    PB_CHECK_U64(pb_cache_lfu.evict(cache), 637539755847373129ULL);
    pb_cache_lfu.on_access(cache, 637543054382257762ULL, false, NULL);
    PB_CHECK_U64(pb_cache_lfu.evict(cache), 637536457312488496ULL);
    /* frequencyOf() is not on the C vtable; step skipped */

    pb_cache_lfu.destroy(cache);
}

/* frequency classes are reused rather than leaked */
static void case_5(void)
{
    pb_rng rng;
    pb_cache_lfu_params params = PB_CACHE_LFU_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 2u;
    cache = pb_cache_lfu.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_lfu.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_lfu.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_lfu.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_lfu.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_lfu.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_lfu.on_access(cache, 12638187200555641996ULL, true, NULL);
    /* frequencyOf() is not on the C vtable; step skipped */
    pb_cache_lfu.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_lfu.on_access(cache, 12638189399578898418ULL, false, NULL);
    PB_CHECK_U64(pb_cache_lfu.evict(cache), 12638190499090526629ULL);
    /* size() is not on the C vtable; step skipped */

    pb_cache_lfu.destroy(cache);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    case_4();
    case_5();
    return pb_test_summary("cache/lfu vectors");
}

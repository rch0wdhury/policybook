/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for cache/arc, produced by scripts/gen-c-vectors.ts from
 * policies/cache/arc/vectors.json. Regenerate with:
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
#include "policybook/cache/arc.h"
#include "policybook/hash.h"
#include "policybook/rng.h"

#include "../pb_test.h"

/* smoke: new keys land in the recent list */
static void case_0(void)
{
    pb_rng rng;
    pb_cache_arc_params params = PB_CACHE_ARC_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 4u;
    cache = pb_cache_arc.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_arc.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638183902020757363ULL, false, NULL);
    /* listOfKey() is not on the C vtable; step skipped */
    /* targetT1() is not on the C vtable; step skipped */
    /* size() is not on the C vtable; step skipped */
    pb_cache_arc.on_access(cache, 12638187200555641996ULL, true, NULL);
    /* listOfKey() is not on the C vtable; step skipped */

    pb_cache_arc.destroy(cache);
}

/* boundary: a capacity of one still tracks both lists */
static void case_1(void)
{
    pb_rng rng;
    pb_cache_arc_params params = PB_CACHE_ARC_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 1u;
    cache = pb_cache_arc.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_arc.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638190499090526629ULL, false, NULL);
    PB_CHECK_U64(pb_cache_arc.evict(cache), 12638187200555641996ULL);
    /* listOfKey() is not on the C vtable; step skipped */
    /* size() is not on the C vtable; step skipped */

    pb_cache_arc.destroy(cache);
}

/* distinguishing: a ghost hit retunes the cache, which LRU and 2Q cannot */
static void case_2(void)
{
    pb_rng rng;
    pb_cache_arc_params params = PB_CACHE_ARC_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 4u;
    cache = pb_cache_arc.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_arc.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638183902020757363ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_arc.on_access(cache, 12638190499090526629ULL, true, NULL);
    /* listOfKey() is not on the C vtable; step skipped */
    pb_cache_arc.on_access(cache, 12638182802509129152ULL, false, NULL);
    PB_CHECK_U64(pb_cache_arc.evict(cache), 12638189399578898418ULL);
    /* listOfKey() is not on the C vtable; step skipped */
    /* targetT1() is not on the C vtable; step skipped */
    pb_cache_arc.on_access(cache, 12638189399578898418ULL, false, NULL);
    /* targetT1() is not on the C vtable; step skipped */
    /* listOfKey() is not on the C vtable; step skipped */
    PB_CHECK_U64(pb_cache_arc.evict(cache), 12638183902020757363ULL);

    pb_cache_arc.destroy(cache);
}

/* tiebreak: replacement takes the oldest of the chosen list */
static void case_3(void)
{
    pb_rng rng;
    pb_cache_arc_params params = PB_CACHE_ARC_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 4u;
    cache = pb_cache_arc.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_arc.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638183902020757363ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_arc.on_access(cache, 12638190499090526629ULL, true, NULL);
    pb_cache_arc.on_access(cache, 12638182802509129152ULL, false, NULL);
    PB_CHECK_U64(pb_cache_arc.evict(cache), 12638189399578898418ULL);
    pb_cache_arc.on_access(cache, 12638186101044013785ULL, false, NULL);
    PB_CHECK_U64(pb_cache_arc.evict(cache), 12638183902020757363ULL);
    pb_cache_arc.on_access(cache, 12638182802509129152ULL, true, NULL);
    pb_cache_arc.on_access(cache, 12638186101044013785ULL, true, NULL);
    /* listOfKey() is not on the C vtable; step skipped */
    pb_cache_arc.on_access(cache, 12638185001532385574ULL, false, NULL);
    PB_CHECK_U64(pb_cache_arc.evict(cache), 12638187200555641996ULL);
    /* listOfKey() is not on the C vtable; step skipped */

    pb_cache_arc.destroy(cache);
}

/* a hit in B2 moves the target the other way */
static void case_4(void)
{
    pb_rng rng;
    pb_cache_arc_params params = PB_CACHE_ARC_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 4u;
    cache = pb_cache_arc.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_arc.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638183902020757363ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_arc.on_access(cache, 12638190499090526629ULL, true, NULL);
    pb_cache_arc.on_access(cache, 12638182802509129152ULL, false, NULL);
    PB_CHECK_U64(pb_cache_arc.evict(cache), 12638189399578898418ULL);
    pb_cache_arc.on_access(cache, 12638189399578898418ULL, false, NULL);
    /* targetT1() is not on the C vtable; step skipped */
    PB_CHECK_U64(pb_cache_arc.evict(cache), 12638183902020757363ULL);
    pb_cache_arc.on_access(cache, 12638186101044013785ULL, false, NULL);
    PB_CHECK_U64(pb_cache_arc.evict(cache), 12638187200555641996ULL);
    /* listOfKey() is not on the C vtable; step skipped */
    pb_cache_arc.on_access(cache, 12638185001532385574ULL, false, NULL);
    PB_CHECK_U64(pb_cache_arc.evict(cache), 12638182802509129152ULL);
    pb_cache_arc.on_access(cache, 12638187200555641996ULL, false, NULL);
    /* targetT1() is not on the C vtable; step skipped */
    /* listOfKey() is not on the C vtable; step skipped */

    pb_cache_arc.destroy(cache);
}

/* the target never leaves [0, capacity] */
static void case_5(void)
{
    pb_rng rng;
    pb_cache_arc_params params = PB_CACHE_ARC_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 2u;
    cache = pb_cache_arc.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_arc.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_arc.on_access(cache, 12638189399578898418ULL, false, NULL);
    PB_CHECK_U64(pb_cache_arc.evict(cache), 12638187200555641996ULL);
    pb_cache_arc.on_access(cache, 12638183902020757363ULL, false, NULL);
    PB_CHECK_U64(pb_cache_arc.evict(cache), 12638190499090526629ULL);
    pb_cache_arc.on_access(cache, 12638182802509129152ULL, false, NULL);
    PB_CHECK_U64(pb_cache_arc.evict(cache), 12638189399578898418ULL);
    /* targetT1() is not on the C vtable; step skipped */
    pb_cache_arc.on_access(cache, 12638183902020757363ULL, true, NULL);
    pb_cache_arc.on_access(cache, 12638186101044013785ULL, false, NULL);
    PB_CHECK_U64(pb_cache_arc.evict(cache), 12638182802509129152ULL);
    pb_cache_arc.on_access(cache, 12638182802509129152ULL, false, NULL);
    /* targetT1() is not on the C vtable; step skipped */

    pb_cache_arc.destroy(cache);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    case_4();
    case_5();
    return pb_test_summary("cache/arc vectors");
}

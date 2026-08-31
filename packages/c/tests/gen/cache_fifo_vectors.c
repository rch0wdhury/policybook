/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for cache/fifo, produced by scripts/gen-c-vectors.ts from
 * policies/cache/fifo/vectors.json. Regenerate with:
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
#include "policybook/cache/fifo.h"
#include "policybook/hash.h"
#include "policybook/rng.h"

#include "../pb_test.h"

/* smoke: entries leave in arrival order */
static void case_0(void)
{
    pb_rng rng;
    pb_cache_fifo_params params = PB_CACHE_FIFO_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 3u;
    cache = pb_cache_fifo.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_fifo.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_fifo.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_fifo.on_access(cache, 12638189399578898418ULL, false, NULL);
    /* size() is not on the C vtable; step skipped */
    pb_cache_fifo.on_access(cache, 12638183902020757363ULL, false, NULL);
    PB_CHECK_U64(pb_cache_fifo.evict(cache), 12638187200555641996ULL);
    PB_CHECK_U64(pb_cache_fifo.evict(cache), 12638190499090526629ULL);
    /* size() is not on the C vtable; step skipped */

    pb_cache_fifo.destroy(cache);
}

/* boundary: a capacity of one holds only the newest key */
static void case_1(void)
{
    pb_rng rng;
    pb_cache_fifo_params params = PB_CACHE_FIFO_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 1u;
    cache = pb_cache_fifo.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_fifo.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_fifo.on_access(cache, 12638190499090526629ULL, false, NULL);
    PB_CHECK_U64(pb_cache_fifo.evict(cache), 12638187200555641996ULL);
    /* size() is not on the C vtable; step skipped */

    pb_cache_fifo.destroy(cache);
}

/* distinguishing: a hit does not save a key, unlike LRU */
static void case_2(void)
{
    pb_rng rng;
    pb_cache_fifo_params params = PB_CACHE_FIFO_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 3u;
    cache = pb_cache_fifo.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_fifo.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_fifo.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_fifo.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_fifo.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_fifo.on_access(cache, 12638183902020757363ULL, false, NULL);
    PB_CHECK_U64(pb_cache_fifo.evict(cache), 12638187200555641996ULL);

    pb_cache_fifo.destroy(cache);
}

/* tiebreak: repeated hits never reorder the queue */
static void case_3(void)
{
    pb_rng rng;
    pb_cache_fifo_params params = PB_CACHE_FIFO_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 3u;
    cache = pb_cache_fifo.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_fifo.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_fifo.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_fifo.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_fifo.on_access(cache, 12638190499090526629ULL, true, NULL);
    pb_cache_fifo.on_access(cache, 12638190499090526629ULL, true, NULL);
    pb_cache_fifo.on_access(cache, 12638189399578898418ULL, true, NULL);
    pb_cache_fifo.on_access(cache, 12638183902020757363ULL, false, NULL);
    PB_CHECK_U64(pb_cache_fifo.evict(cache), 12638187200555641996ULL);
    PB_CHECK_U64(pb_cache_fifo.evict(cache), 12638190499090526629ULL);
    PB_CHECK_U64(pb_cache_fifo.evict(cache), 12638189399578898418ULL);
    PB_CHECK_U64(pb_cache_fifo.evict(cache), 12638183902020757363ULL);

    pb_cache_fifo.destroy(cache);
}

/* reuse: an evicted slot is reused without disturbing order */
static void case_4(void)
{
    pb_rng rng;
    pb_cache_fifo_params params = PB_CACHE_FIFO_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 2u;
    cache = pb_cache_fifo.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_fifo.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_fifo.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_fifo.on_access(cache, 12638189399578898418ULL, false, NULL);
    PB_CHECK_U64(pb_cache_fifo.evict(cache), 12638187200555641996ULL);
    pb_cache_fifo.on_access(cache, 12638183902020757363ULL, false, NULL);
    PB_CHECK_U64(pb_cache_fifo.evict(cache), 12638190499090526629ULL);
    PB_CHECK_U64(pb_cache_fifo.evict(cache), 12638189399578898418ULL);
    PB_CHECK_U64(pb_cache_fifo.evict(cache), 12638183902020757363ULL);
    /* size() is not on the C vtable; step skipped */

    pb_cache_fifo.destroy(cache);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    case_4();
    return pb_test_summary("cache/fifo vectors");
}

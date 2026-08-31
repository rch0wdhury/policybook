/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for cache/s3-fifo, produced by scripts/gen-c-vectors.ts from
 * policies/cache/s3-fifo/vectors.json. Regenerate with:
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
#include "policybook/cache/s3_fifo.h"
#include "policybook/hash.h"
#include "policybook/rng.h"

#include "../pb_test.h"

/* smoke: new keys audition in the small queue */
static void case_0(void)
{
    pb_rng rng;
    pb_cache_s3_fifo_params params = PB_CACHE_S3_FIFO_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 10u;
    params.small_fraction = 0.3;
    cache = pb_cache_s3_fifo.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, false, NULL);
    /* queueOf() is not on the C vtable; step skipped */
    /* frequencyOf() is not on the C vtable; step skipped */
    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, true, NULL);
    /* frequencyOf() is not on the C vtable; step skipped */
    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, true, NULL);
    /* frequencyOf() is not on the C vtable; step skipped */
    /* queueOf() is not on the C vtable; step skipped */
    /* size() is not on the C vtable; step skipped */

    pb_cache_s3_fifo.destroy(cache);
}

/* boundary: the counter saturates at two bits */
static void case_1(void)
{
    pb_rng rng;
    pb_cache_s3_fifo_params params = PB_CACHE_S3_FIFO_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 10u;
    params.small_fraction = 0.3;
    cache = pb_cache_s3_fifo.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, true, NULL);
    /* frequencyOf() is not on the C vtable; step skipped */
    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, true, NULL);
    /* frequencyOf() is not on the C vtable; step skipped */

    pb_cache_s3_fifo.destroy(cache);
}

/* distinguishing: a one-hit wonder dies in the small queue */
static void case_2(void)
{
    pb_rng rng;
    pb_cache_s3_fifo_params params = PB_CACHE_S3_FIFO_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 10u;
    params.small_fraction = 0.3;
    cache = pb_cache_s3_fifo.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638183902020757363ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638182802509129152ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638186101044013785ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638185001532385574ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638197096160295895ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638195996648667684ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638199295183552317ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638198195671924106ULL, false, NULL);
    PB_CHECK_U64(pb_cache_s3_fifo.evict(cache), 12638190499090526629ULL);
    /* queueOf() is not on the C vtable; step skipped */
    /* queueOf() is not on the C vtable; step skipped */

    pb_cache_s3_fifo.destroy(cache);
}

/* tiebreak: each queue is strictly FIFO */
static void case_3(void)
{
    pb_rng rng;
    pb_cache_s3_fifo_params params = PB_CACHE_S3_FIFO_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 10u;
    params.small_fraction = 0.3;
    cache = pb_cache_s3_fifo.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638183902020757363ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638182802509129152ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638186101044013785ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638185001532385574ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638197096160295895ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638195996648667684ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638199295183552317ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638198195671924106ULL, false, NULL);
    PB_CHECK_U64(pb_cache_s3_fifo.evict(cache), 12638187200555641996ULL);
    pb_cache_s3_fifo.on_access(cache, 12638192698113783051ULL, false, NULL);
    PB_CHECK_U64(pb_cache_s3_fifo.evict(cache), 12638190499090526629ULL);
    pb_cache_s3_fifo.on_access(cache, 12638191598602154840ULL, false, NULL);
    PB_CHECK_U64(pb_cache_s3_fifo.evict(cache), 12638189399578898418ULL);

    pb_cache_s3_fifo.destroy(cache);
}

/* a returning ghost skips the audition */
static void case_4(void)
{
    pb_rng rng;
    pb_cache_s3_fifo_params params = PB_CACHE_S3_FIFO_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 10u;
    params.small_fraction = 0.3;
    cache = pb_cache_s3_fifo.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638183902020757363ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638182802509129152ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638186101044013785ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638185001532385574ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638197096160295895ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638195996648667684ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638199295183552317ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638198195671924106ULL, false, NULL);
    PB_CHECK_U64(pb_cache_s3_fifo.evict(cache), 12638187200555641996ULL);
    /* queueOf() is not on the C vtable; step skipped */
    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, false, NULL);
    /* queueOf() is not on the C vtable; step skipped */
    /* queueOf() is not on the C vtable; step skipped */

    pb_cache_s3_fifo.destroy(cache);
}

/* the main queue spends second chances before evicting */
static void case_5(void)
{
    pb_rng rng;
    pb_cache_s3_fifo_params params = PB_CACHE_S3_FIFO_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 4u;
    params.small_fraction = 0.25;
    cache = pb_cache_s3_fifo.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, true, NULL);
    /* frequencyOf() is not on the C vtable; step skipped */
    pb_cache_s3_fifo.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638183902020757363ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638182802509129152ULL, false, NULL);
    PB_CHECK_U64(pb_cache_s3_fifo.evict(cache), 12638190499090526629ULL);
    /* queueOf() is not on the C vtable; step skipped */
    pb_cache_s3_fifo.on_access(cache, 12638186101044013785ULL, false, NULL);
    PB_CHECK_U64(pb_cache_s3_fifo.evict(cache), 12638189399578898418ULL);

    pb_cache_s3_fifo.destroy(cache);
}

/* distinguishing: promotion while G is full keeps the other ghosts */
static void case_6(void)
{
    pb_rng rng;
    pb_cache_s3_fifo_params params = PB_CACHE_S3_FIFO_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 3u;
    params.small_fraction = 0.1;
    cache = pb_cache_s3_fifo.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_s3_fifo.on_access(cache, 12638183902020757363ULL, false, NULL);
    PB_CHECK_U64(pb_cache_s3_fifo.evict(cache), 12638187200555641996ULL);
    pb_cache_s3_fifo.on_access(cache, 12638182802509129152ULL, false, NULL);
    PB_CHECK_U64(pb_cache_s3_fifo.evict(cache), 12638190499090526629ULL);
    pb_cache_s3_fifo.on_access(cache, 12638190499090526629ULL, false, NULL);
    PB_CHECK_U64(pb_cache_s3_fifo.evict(cache), 12638189399578898418ULL);
    /* queueOf() is not on the C vtable; step skipped */
    /* queueOf() is not on the C vtable; step skipped */
    pb_cache_s3_fifo.on_access(cache, 12638187200555641996ULL, false, NULL);
    /* queueOf() is not on the C vtable; step skipped */
    PB_CHECK_U64(pb_cache_s3_fifo.evict(cache), 12638183902020757363ULL);
    pb_cache_s3_fifo.on_access(cache, 12638186101044013785ULL, false, NULL);
    PB_CHECK_U64(pb_cache_s3_fifo.evict(cache), 12638182802509129152ULL);
    pb_cache_s3_fifo.on_access(cache, 12638185001532385574ULL, false, NULL);
    PB_CHECK_U64(pb_cache_s3_fifo.evict(cache), 12638186101044013785ULL);

    pb_cache_s3_fifo.destroy(cache);
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
    return pb_test_summary("cache/s3-fifo vectors");
}

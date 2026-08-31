/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for cache/2q, produced by scripts/gen-c-vectors.ts from
 * policies/cache/2q/vectors.json. Regenerate with:
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
#include "policybook/cache/2q.h"
#include "policybook/hash.h"
#include "policybook/rng.h"

#include "../pb_test.h"

/* smoke: new keys audition in A1in and leave in arrival order */
static void case_0(void)
{
    pb_rng rng;
    pb_cache_2q_params params = PB_CACHE_2Q_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 4u;
    params.kin = 0.25;
    params.kout = 0.5;
    cache = pb_cache_2q.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_2q.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638183902020757363ULL, false, NULL);
    /* queueOf() is not on the C vtable; step skipped */
    /* size() is not on the C vtable; step skipped */
    pb_cache_2q.on_access(cache, 12638182802509129152ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638187200555641996ULL);
    /* queueOf() is not on the C vtable; step skipped */

    pb_cache_2q.destroy(cache);
}

/* boundary: a capacity of one still remembers one ghost */
static void case_1(void)
{
    pb_rng rng;
    pb_cache_2q_params params = PB_CACHE_2Q_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 1u;
    params.kin = 0.25;
    params.kout = 0.5;
    cache = pb_cache_2q.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_2q.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638190499090526629ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638187200555641996ULL);
    /* queueOf() is not on the C vtable; step skipped */
    /* size() is not on the C vtable; step skipped */

    pb_cache_2q.destroy(cache);
}

/* distinguishing: a scan cannot reach the main cache, unlike LRU */
static void case_2(void)
{
    pb_rng rng;
    pb_cache_2q_params params = PB_CACHE_2Q_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 4u;
    params.kin = 0.25;
    params.kout = 0.5;
    cache = pb_cache_2q.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_2q.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638183902020757363ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638182802509129152ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638187200555641996ULL);
    pb_cache_2q.on_access(cache, 12638187200555641996ULL, false, NULL);
    /* queueOf() is not on the C vtable; step skipped */
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638190499090526629ULL);
    pb_cache_2q.on_access(cache, 12638186101044013785ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638189399578898418ULL);
    pb_cache_2q.on_access(cache, 12638185001532385574ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638183902020757363ULL);
    pb_cache_2q.on_access(cache, 12638197096160295895ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638182802509129152ULL);
    pb_cache_2q.on_access(cache, 12638195996648667684ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638186101044013785ULL);
    /* queueOf() is not on the C vtable; step skipped */

    pb_cache_2q.destroy(cache);
}

/* tiebreak: A1in is strictly FIFO and Am is strictly LRU */
static void case_3(void)
{
    pb_rng rng;
    pb_cache_2q_params params = PB_CACHE_2Q_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 6u;
    params.kin = 0.5;
    params.kout = 0.5;
    cache = pb_cache_2q.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_2q.on_access(cache, 12638214688346347271ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638213588834719060ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638205892253321583ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638204792741693372ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638208091276578005ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638206991764949794ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638201494206808739ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638214688346347271ULL);
    pb_cache_2q.on_access(cache, 12638200394695180528ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638213588834719060ULL);
    pb_cache_2q.on_access(cache, 12638214688346347271ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638205892253321583ULL);
    pb_cache_2q.on_access(cache, 12638213588834719060ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638204792741693372ULL);
    /* queueOf() is not on the C vtable; step skipped */
    /* queueOf() is not on the C vtable; step skipped */
    pb_cache_2q.on_access(cache, 12638203693230065161ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638208091276578005ULL);
    pb_cache_2q.on_access(cache, 12638202593718436950ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638206991764949794ULL);

    pb_cache_2q.destroy(cache);
}

/* a hit inside A1in does not promote or reorder */
static void case_4(void)
{
    pb_rng rng;
    pb_cache_2q_params params = PB_CACHE_2Q_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 4u;
    params.kin = 0.25;
    params.kout = 0.5;
    cache = pb_cache_2q.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_2q.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638187200555641996ULL, true, NULL);
    pb_cache_2q.on_access(cache, 12638187200555641996ULL, true, NULL);
    /* queueOf() is not on the C vtable; step skipped */
    pb_cache_2q.on_access(cache, 12638183902020757363ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638182802509129152ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638187200555641996ULL);

    pb_cache_2q.destroy(cache);
}

/* a ghost that expires loses its promotion */
static void case_5(void)
{
    pb_rng rng;
    pb_cache_2q_params params = PB_CACHE_2Q_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 4u;
    params.kin = 0.25;
    params.kout = 0.5;
    cache = pb_cache_2q.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_2q.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638183902020757363ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638182802509129152ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638187200555641996ULL);
    pb_cache_2q.on_access(cache, 12638186101044013785ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638190499090526629ULL);
    pb_cache_2q.on_access(cache, 12638185001532385574ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638189399578898418ULL);
    /* queueOf() is not on the C vtable; step skipped */
    pb_cache_2q.on_access(cache, 12638187200555641996ULL, false, NULL);
    /* queueOf() is not on the C vtable; step skipped */

    pb_cache_2q.destroy(cache);
}

/* distinguishing: promotion while A1out is full keeps the other ghosts */
static void case_6(void)
{
    pb_rng rng;
    pb_cache_2q_params params = PB_CACHE_2Q_PARAMS_DEFAULT;
    pb_cache *cache;

    pb_rng_init(&rng, 1u);
    params.capacity = 4u;
    params.kin = 0.25;
    params.kout = 0.75;
    cache = pb_cache_2q.create(&params, NULL, &rng);
    PB_CHECK(cache != NULL);
    if (cache == NULL) {
        return;
    }

    pb_cache_2q.on_access(cache, 12638187200555641996ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638190499090526629ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638189399578898418ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638183902020757363ULL, false, NULL);
    pb_cache_2q.on_access(cache, 12638182802509129152ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638187200555641996ULL);
    pb_cache_2q.on_access(cache, 12638186101044013785ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638190499090526629ULL);
    pb_cache_2q.on_access(cache, 12638185001532385574ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638189399578898418ULL);
    pb_cache_2q.on_access(cache, 12638190499090526629ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638183902020757363ULL);
    /* queueOf() is not on the C vtable; step skipped */
    /* queueOf() is not on the C vtable; step skipped */
    /* queueOf() is not on the C vtable; step skipped */
    pb_cache_2q.on_access(cache, 12638187200555641996ULL, false, NULL);
    /* queueOf() is not on the C vtable; step skipped */
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638182802509129152ULL);
    pb_cache_2q.on_access(cache, 12638197096160295895ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638186101044013785ULL);
    pb_cache_2q.on_access(cache, 12638195996648667684ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638185001532385574ULL);
    pb_cache_2q.on_access(cache, 12638199295183552317ULL, false, NULL);
    PB_CHECK_U64(pb_cache_2q.evict(cache), 12638197096160295895ULL);

    pb_cache_2q.destroy(cache);
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
    return pb_test_summary("cache/2q vectors");
}

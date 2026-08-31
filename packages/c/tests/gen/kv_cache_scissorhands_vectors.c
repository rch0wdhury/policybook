/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for kv-cache/scissorhands, produced by scripts/gen-c-vectors.ts from
 * policies/kv-cache/scissorhands/vectors.json. Regenerate with:
 *
 *     pnpm gen:c-vectors
 *
 * The C implementation is conformant when it reproduces these results, which
 * are the same ones the TypeScript and Python implementations are held to.
 */

#include <stddef.h>
#include <stdint.h>

#include "policybook/kv_cache/kv_cache.h"
#include "policybook/kv_cache/scissorhands.h"
#include "policybook/rng.h"

#include "../pb_test.h"

#define VICTIM_CAPACITY 3

static const float attn_0_0[] = { 1.0f };
static const float attn_0_2[] = { 0.25f, 0.75f };
static const float attn_0_3[] = { 0.5f, 0.25f, 0.25f };

/* smoke: the fewest votes goes */
static void case_0(void)
{
    pb_rng rng;
    pb_kvcache_scissorhands_params params = PB_KVCACHE_SCISSORHANDS_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 3u;
    params.recent_window = 1u;
    policy = pb_kvcache_scissorhands.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_scissorhands.on_decode_step(policy, 1u, attn_0_0,
                             sizeof(attn_0_0) / sizeof(attn_0_0[0]));
    /* votesOf() is not on the C vtable; step skipped */
    pb_kvcache_scissorhands.on_decode_step(policy, 2u, attn_0_2,
                             sizeof(attn_0_2) / sizeof(attn_0_2[0]));
    pb_kvcache_scissorhands.on_decode_step(policy, 3u, attn_0_3,
                             sizeof(attn_0_3) / sizeof(attn_0_3[0]));
    /* votesOf() is not on the C vtable; step skipped */
    /* votesOf() is not on the C vtable; step skipped */
    /* votesOf() is not on the C vtable; step skipped */
    evicted = pb_kvcache_scissorhands.evict(policy, 3u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 2);
    }
    /* keptCount() is not on the C vtable; step skipped */

    pb_kvcache_scissorhands.destroy(policy);
}

static const float attn_1_0[] = { 1.0f };
static const float attn_1_1[] = { 0.5f, 0.5f };
static const float attn_1_4[] = { 0.25f, 0.25f, 0.5f };

/* boundary: exactly matching the fair share earns nothing */
static void case_1(void)
{
    pb_rng rng;
    pb_kvcache_scissorhands_params params = PB_KVCACHE_SCISSORHANDS_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 3u;
    params.recent_window = 1u;
    policy = pb_kvcache_scissorhands.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_scissorhands.on_decode_step(policy, 1u, attn_1_0,
                             sizeof(attn_1_0) / sizeof(attn_1_0[0]));
    pb_kvcache_scissorhands.on_decode_step(policy, 2u, attn_1_1,
                             sizeof(attn_1_1) / sizeof(attn_1_1[0]));
    /* votesOf() is not on the C vtable; step skipped */
    /* votesOf() is not on the C vtable; step skipped */
    pb_kvcache_scissorhands.on_decode_step(policy, 3u, attn_1_4,
                             sizeof(attn_1_4) / sizeof(attn_1_4[0]));
    /* votesOf() is not on the C vtable; step skipped */
    evicted = pb_kvcache_scissorhands.evict(policy, 3u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 0);
    }

    pb_kvcache_scissorhands.destroy(policy);
}

static const float attn_2_0[] = { 1.0f };
static const float attn_2_1[] = { 0.9375f, 0.0625f };
static const float attn_2_2[] = { 0.5f, 0.25f, 0.25f };

/* boundary: the recent window outranks the votes */
static void case_2(void)
{
    pb_rng rng;
    pb_kvcache_scissorhands_params params = PB_KVCACHE_SCISSORHANDS_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 3u;
    params.recent_window = 2u;
    policy = pb_kvcache_scissorhands.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_scissorhands.on_decode_step(policy, 1u, attn_2_0,
                             sizeof(attn_2_0) / sizeof(attn_2_0[0]));
    pb_kvcache_scissorhands.on_decode_step(policy, 2u, attn_2_1,
                             sizeof(attn_2_1) / sizeof(attn_2_1[0]));
    pb_kvcache_scissorhands.on_decode_step(policy, 3u, attn_2_2,
                             sizeof(attn_2_2) / sizeof(attn_2_2[0]));
    /* votesOf() is not on the C vtable; step skipped */
    /* votesOf() is not on the C vtable; step skipped */
    /* votesOf() is not on the C vtable; step skipped */
    evicted = pb_kvcache_scissorhands.evict(policy, 3u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 1);
    }
    /* votesOf() is not on the C vtable; step skipped */

    pb_kvcache_scissorhands.destroy(policy);
}

static const float attn_3_0[] = { 1.0f };
static const float attn_3_1[] = { 0.875f, 0.125f };
static const float attn_3_2[] = { 0.0625f, 0.5f, 0.4375f };
static const float attn_3_3[] = { 0.0625f, 0.3125f, 0.3125f, 0.3125f };
static const float attn_3_4[] = { 0.0625f, 0.25f, 0.25f, 0.25f, 0.1875f };

/* distinguishing: an early spike buys one vote and no more */
static void case_3(void)
{
    pb_rng rng;
    pb_kvcache_scissorhands_params params = PB_KVCACHE_SCISSORHANDS_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 5u;
    params.recent_window = 2u;
    policy = pb_kvcache_scissorhands.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_scissorhands.on_decode_step(policy, 1u, attn_3_0,
                             sizeof(attn_3_0) / sizeof(attn_3_0[0]));
    pb_kvcache_scissorhands.on_decode_step(policy, 2u, attn_3_1,
                             sizeof(attn_3_1) / sizeof(attn_3_1[0]));
    pb_kvcache_scissorhands.on_decode_step(policy, 3u, attn_3_2,
                             sizeof(attn_3_2) / sizeof(attn_3_2[0]));
    pb_kvcache_scissorhands.on_decode_step(policy, 4u, attn_3_3,
                             sizeof(attn_3_3) / sizeof(attn_3_3[0]));
    pb_kvcache_scissorhands.on_decode_step(policy, 5u, attn_3_4,
                             sizeof(attn_3_4) / sizeof(attn_3_4[0]));
    /* votesOf() is not on the C vtable; step skipped */
    /* votesOf() is not on the C vtable; step skipped */
    /* votesOf() is not on the C vtable; step skipped */
    /* votesOf() is not on the C vtable; step skipped */
    /* votesOf() is not on the C vtable; step skipped */
    evicted = pb_kvcache_scissorhands.evict(policy, 5u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 0);
    }
    /* votesOf() is not on the C vtable; step skipped */

    pb_kvcache_scissorhands.destroy(policy);
}

static const float attn_4_0[] = { 1.0f };
static const float attn_4_1[] = { 0.875f, 0.125f };
static const float attn_4_2[] = { 0.5f, 0.25f, 0.25f };

/* tiebreak: equal votes evict the lower position */
static void case_4(void)
{
    pb_rng rng;
    pb_kvcache_scissorhands_params params = PB_KVCACHE_SCISSORHANDS_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 3u;
    params.recent_window = 1u;
    policy = pb_kvcache_scissorhands.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_scissorhands.on_decode_step(policy, 1u, attn_4_0,
                             sizeof(attn_4_0) / sizeof(attn_4_0[0]));
    pb_kvcache_scissorhands.on_decode_step(policy, 2u, attn_4_1,
                             sizeof(attn_4_1) / sizeof(attn_4_1[0]));
    pb_kvcache_scissorhands.on_decode_step(policy, 3u, attn_4_2,
                             sizeof(attn_4_2) / sizeof(attn_4_2[0]));
    /* votesOf() is not on the C vtable; step skipped */
    /* votesOf() is not on the C vtable; step skipped */
    /* votesOf() is not on the C vtable; step skipped */
    evicted = pb_kvcache_scissorhands.evict(policy, 3u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 1);
    }

    pb_kvcache_scissorhands.destroy(policy);
}

static const float attn_5_0[] = { 1.0f };
static const float attn_5_1[] = { 0.75f, 0.25f };
static const float attn_5_2[] = { 0.5f, 0.25f, 0.25f };
static const float attn_5_3[] = { 0.5f, 0.125f, 0.25f, 0.125f };

/* several victims come back in position order, not in the order chosen */
static void case_5(void)
{
    pb_rng rng;
    pb_kvcache_scissorhands_params params = PB_KVCACHE_SCISSORHANDS_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 5u;
    params.recent_window = 1u;
    policy = pb_kvcache_scissorhands.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_scissorhands.on_decode_step(policy, 1u, attn_5_0,
                             sizeof(attn_5_0) / sizeof(attn_5_0[0]));
    pb_kvcache_scissorhands.on_decode_step(policy, 2u, attn_5_1,
                             sizeof(attn_5_1) / sizeof(attn_5_1[0]));
    pb_kvcache_scissorhands.on_decode_step(policy, 3u, attn_5_2,
                             sizeof(attn_5_2) / sizeof(attn_5_2[0]));
    pb_kvcache_scissorhands.on_decode_step(policy, 4u, attn_5_3,
                             sizeof(attn_5_3) / sizeof(attn_5_3[0]));
    /* votesOf() is not on the C vtable; step skipped */
    /* votesOf() is not on the C vtable; step skipped */
    /* votesOf() is not on the C vtable; step skipped */
    /* votesOf() is not on the C vtable; step skipped */
    evicted = pb_kvcache_scissorhands.evict(policy, 2u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 3);
    if (evicted == 3) {
        PB_CHECK_U64(victims[0], 1);
        PB_CHECK_U64(victims[1], 2);
        PB_CHECK_U64(victims[2], 3);
    }
    /* keptCount() is not on the C vtable; step skipped */

    pb_kvcache_scissorhands.destroy(policy);
}

static const float attn_6_0[] = { 1.0f };
static const float attn_6_1[] = { 0.75f, 0.25f };

/* a step with no attention leaves every vote untouched */
static void case_6(void)
{
    pb_rng rng;
    pb_kvcache_scissorhands_params params = PB_KVCACHE_SCISSORHANDS_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 3u;
    params.recent_window = 1u;
    policy = pb_kvcache_scissorhands.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_scissorhands.on_decode_step(policy, 1u, attn_6_0,
                             sizeof(attn_6_0) / sizeof(attn_6_0[0]));
    pb_kvcache_scissorhands.on_decode_step(policy, 2u, attn_6_1,
                             sizeof(attn_6_1) / sizeof(attn_6_1[0]));
    /* votesOf() is not on the C vtable; step skipped */
    pb_kvcache_scissorhands.on_decode_step(policy, 3u, NULL, 0);
    /* votesOf() is not on the C vtable; step skipped */
    /* votesOf() is not on the C vtable; step skipped */
    evicted = pb_kvcache_scissorhands.evict(policy, 3u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 1);
    }

    pb_kvcache_scissorhands.destroy(policy);
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
    return pb_test_summary("kv-cache/scissorhands vectors");
}

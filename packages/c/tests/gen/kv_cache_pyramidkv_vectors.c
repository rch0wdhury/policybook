/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for kv-cache/pyramidkv, produced by scripts/gen-c-vectors.ts from
 * policies/kv-cache/pyramidkv/vectors.json. Regenerate with:
 *
 *     pnpm gen:c-vectors
 *
 * The C implementation is conformant when it reproduces these results, which
 * are the same ones the TypeScript and Python implementations are held to.
 */

#include <stddef.h>
#include <stdint.h>

#include "policybook/kv_cache/kv_cache.h"
#include "policybook/kv_cache/pyramidkv.h"
#include "policybook/rng.h"

#include "../pb_test.h"

#define VICTIM_CAPACITY 3

static const float attn_0_0[] = { 1.0f };
static const float attn_0_1[] = { 0.5f, 0.5f };
static const float attn_0_2[] = { 0.25f, 0.25f, 0.5f };
static const float attn_0_3[] = { 0.125f, 0.125f, 0.25f, 0.5f };
static const float attn_0_4[] = { 0.0625f, 0.0625f, 0.125f, 0.25f, 0.5f };
static const float attn_0_5[] = { 0.0625f, 0.0625f, 0.0625f, 0.125f, 0.25f, 0.4375f };

/* smoke: one layer redistributes nothing */
static void case_0(void)
{
    pb_rng rng;
    pb_kvcache_pyramidkv_params params = PB_KVCACHE_PYRAMIDKV_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 6u;
    params.layer = 0u;
    params.num_layers = 1u;
    params.recent_window = 1u;
    params.obs_window = 8u;
    params.pool_kernel = 1u;
    policy = pb_kvcache_pyramidkv.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_pyramidkv.on_decode_step(policy, 1u, attn_0_0,
                             sizeof(attn_0_0) / sizeof(attn_0_0[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 2u, attn_0_1,
                             sizeof(attn_0_1) / sizeof(attn_0_1[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 3u, attn_0_2,
                             sizeof(attn_0_2) / sizeof(attn_0_2[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 4u, attn_0_3,
                             sizeof(attn_0_3) / sizeof(attn_0_3[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 5u, attn_0_4,
                             sizeof(attn_0_4) / sizeof(attn_0_4[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 6u, attn_0_5,
                             sizeof(attn_0_5) / sizeof(attn_0_5[0]));
    /* effectiveBudget() is not on the C vtable; step skipped */
    /* windowScoreOf() is not on the C vtable; step skipped */
    /* windowScoreOf() is not on the C vtable; step skipped */
    evicted = pb_kvcache_pyramidkv.evict(policy, 6u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 5);
    }
    /* keptCount() is not on the C vtable; step skipped */

    pb_kvcache_pyramidkv.destroy(policy);
}

static const float attn_1_0[] = { 1.0f };
static const float attn_1_1[] = { 0.5f, 0.5f };
static const float attn_1_2[] = { 0.25f, 0.25f, 0.5f };
static const float attn_1_3[] = { 0.125f, 0.125f, 0.25f, 0.5f };
static const float attn_1_4[] = { 0.0625f, 0.0625f, 0.125f, 0.25f, 0.5f };
static const float attn_1_5[] = { 0.0625f, 0.0625f, 0.0625f, 0.125f, 0.25f, 0.4375f };

/* distinguishing: a deep layer's share binds tighter than the ask */
static void case_1(void)
{
    pb_rng rng;
    pb_kvcache_pyramidkv_params params = PB_KVCACHE_PYRAMIDKV_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 6u;
    params.layer = 2u;
    params.num_layers = 3u;
    params.pyramid_ratio = 2u;
    params.recent_window = 1u;
    params.obs_window = 8u;
    params.pool_kernel = 1u;
    policy = pb_kvcache_pyramidkv.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_pyramidkv.on_decode_step(policy, 1u, attn_1_0,
                             sizeof(attn_1_0) / sizeof(attn_1_0[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 2u, attn_1_1,
                             sizeof(attn_1_1) / sizeof(attn_1_1[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 3u, attn_1_2,
                             sizeof(attn_1_2) / sizeof(attn_1_2[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 4u, attn_1_3,
                             sizeof(attn_1_3) / sizeof(attn_1_3[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 5u, attn_1_4,
                             sizeof(attn_1_4) / sizeof(attn_1_4[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 6u, attn_1_5,
                             sizeof(attn_1_5) / sizeof(attn_1_5[0]));
    /* effectiveBudget() is not on the C vtable; step skipped */
    evicted = pb_kvcache_pyramidkv.evict(policy, 6u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 3);
    if (evicted == 3) {
        PB_CHECK_U64(victims[0], 3);
        PB_CHECK_U64(victims[1], 4);
        PB_CHECK_U64(victims[2], 5);
    }
    /* keptCount() is not on the C vtable; step skipped */

    pb_kvcache_pyramidkv.destroy(policy);
}

static const float attn_2_0[] = { 1.0f };
static const float attn_2_1[] = { 0.5f, 0.5f };
static const float attn_2_2[] = { 0.25f, 0.25f, 0.5f };
static const float attn_2_3[] = { 0.125f, 0.125f, 0.25f, 0.5f };
static const float attn_2_4[] = { 0.0625f, 0.0625f, 0.125f, 0.25f, 0.5f };
static const float attn_2_5[] = { 0.0625f, 0.0625f, 0.0625f, 0.125f, 0.25f, 0.4375f };

/* boundary: a shallow layer's share cannot exceed what the caller allows */
static void case_2(void)
{
    pb_rng rng;
    pb_kvcache_pyramidkv_params params = PB_KVCACHE_PYRAMIDKV_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 6u;
    params.layer = 0u;
    params.num_layers = 3u;
    params.pyramid_ratio = 2u;
    params.recent_window = 1u;
    params.obs_window = 8u;
    params.pool_kernel = 1u;
    policy = pb_kvcache_pyramidkv.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_pyramidkv.on_decode_step(policy, 1u, attn_2_0,
                             sizeof(attn_2_0) / sizeof(attn_2_0[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 2u, attn_2_1,
                             sizeof(attn_2_1) / sizeof(attn_2_1[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 3u, attn_2_2,
                             sizeof(attn_2_2) / sizeof(attn_2_2[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 4u, attn_2_3,
                             sizeof(attn_2_3) / sizeof(attn_2_3[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 5u, attn_2_4,
                             sizeof(attn_2_4) / sizeof(attn_2_4[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 6u, attn_2_5,
                             sizeof(attn_2_5) / sizeof(attn_2_5[0]));
    /* effectiveBudget() is not on the C vtable; step skipped */
    evicted = pb_kvcache_pyramidkv.evict(policy, 6u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 5);
    }
    /* keptCount() is not on the C vtable; step skipped */

    pb_kvcache_pyramidkv.destroy(policy);
}

static const float attn_3_0[] = { 1.0f };
static const float attn_3_1[] = { 0.5f, 0.5f };
static const float attn_3_2[] = { 0.25f, 0.25f, 0.5f };
static const float attn_3_3[] = { 0.125f, 0.125f, 0.25f, 0.5f };
static const float attn_3_4[] = { 0.0625f, 0.0625f, 0.125f, 0.25f, 0.5f };
static const float attn_3_5[] = { 0.0625f, 0.0625f, 0.0625f, 0.125f, 0.25f, 0.4375f };

/* the middle layer of three gets exactly the average */
static void case_3(void)
{
    pb_rng rng;
    pb_kvcache_pyramidkv_params params = PB_KVCACHE_PYRAMIDKV_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 6u;
    params.layer = 1u;
    params.num_layers = 3u;
    params.pyramid_ratio = 2u;
    params.recent_window = 1u;
    params.obs_window = 8u;
    params.pool_kernel = 1u;
    policy = pb_kvcache_pyramidkv.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_pyramidkv.on_decode_step(policy, 1u, attn_3_0,
                             sizeof(attn_3_0) / sizeof(attn_3_0[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 2u, attn_3_1,
                             sizeof(attn_3_1) / sizeof(attn_3_1[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 3u, attn_3_2,
                             sizeof(attn_3_2) / sizeof(attn_3_2[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 4u, attn_3_3,
                             sizeof(attn_3_3) / sizeof(attn_3_3[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 5u, attn_3_4,
                             sizeof(attn_3_4) / sizeof(attn_3_4[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 6u, attn_3_5,
                             sizeof(attn_3_5) / sizeof(attn_3_5[0]));
    /* effectiveBudget() is not on the C vtable; step skipped */
    evicted = pb_kvcache_pyramidkv.evict(policy, 6u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 5);
    }

    pb_kvcache_pyramidkv.destroy(policy);
}

static const float attn_4_0[] = { 1.0f };
static const float attn_4_1[] = { 0.5f, 0.5f };
static const float attn_4_2[] = { 0.25f, 0.25f, 0.5f };
static const float attn_4_3[] = { 0.125f, 0.125f, 0.25f, 0.5f };
static const float attn_4_4[] = { 0.0625f, 0.0625f, 0.125f, 0.25f, 0.5f };
static const float attn_4_5[] = { 0.0625f, 0.0625f, 0.0625f, 0.125f, 0.25f, 0.4375f };

/* tiebreak: a ratio of one is the degenerate uniform pyramid */
static void case_4(void)
{
    pb_rng rng;
    pb_kvcache_pyramidkv_params params = PB_KVCACHE_PYRAMIDKV_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 6u;
    params.layer = 2u;
    params.num_layers = 3u;
    params.pyramid_ratio = 1u;
    params.recent_window = 1u;
    params.obs_window = 8u;
    params.pool_kernel = 1u;
    policy = pb_kvcache_pyramidkv.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_pyramidkv.on_decode_step(policy, 1u, attn_4_0,
                             sizeof(attn_4_0) / sizeof(attn_4_0[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 2u, attn_4_1,
                             sizeof(attn_4_1) / sizeof(attn_4_1[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 3u, attn_4_2,
                             sizeof(attn_4_2) / sizeof(attn_4_2[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 4u, attn_4_3,
                             sizeof(attn_4_3) / sizeof(attn_4_3[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 5u, attn_4_4,
                             sizeof(attn_4_4) / sizeof(attn_4_4[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 6u, attn_4_5,
                             sizeof(attn_4_5) / sizeof(attn_4_5[0]));
    /* effectiveBudget() is not on the C vtable; step skipped */
    evicted = pb_kvcache_pyramidkv.evict(policy, 6u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 5);
    }

    pb_kvcache_pyramidkv.destroy(policy);
}

static const float attn_5_0[] = { 1.0f };
static const float attn_5_1[] = { 0.5f, 0.5f };
static const float attn_5_2[] = { 0.25f, 0.25f, 0.5f };
static const float attn_5_3[] = { 0.125f, 0.125f, 0.25f, 0.5f };
static const float attn_5_4[] = { 0.0625f, 0.0625f, 0.125f, 0.25f, 0.5f };
static const float attn_5_5[] = { 0.0625f, 0.0625f, 0.0625f, 0.125f, 0.25f, 0.4375f };

/* a share below the recent window is raised to hold one choosable position */
static void case_5(void)
{
    pb_rng rng;
    pb_kvcache_pyramidkv_params params = PB_KVCACHE_PYRAMIDKV_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 10u;
    params.layer = 3u;
    params.num_layers = 4u;
    params.pyramid_ratio = 4u;
    params.recent_window = 4u;
    params.obs_window = 8u;
    params.pool_kernel = 1u;
    policy = pb_kvcache_pyramidkv.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_pyramidkv.on_decode_step(policy, 1u, attn_5_0,
                             sizeof(attn_5_0) / sizeof(attn_5_0[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 2u, attn_5_1,
                             sizeof(attn_5_1) / sizeof(attn_5_1[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 3u, attn_5_2,
                             sizeof(attn_5_2) / sizeof(attn_5_2[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 4u, attn_5_3,
                             sizeof(attn_5_3) / sizeof(attn_5_3[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 5u, attn_5_4,
                             sizeof(attn_5_4) / sizeof(attn_5_4[0]));
    pb_kvcache_pyramidkv.on_decode_step(policy, 6u, attn_5_5,
                             sizeof(attn_5_5) / sizeof(attn_5_5[0]));
    /* effectiveBudget() is not on the C vtable; step skipped */
    evicted = pb_kvcache_pyramidkv.evict(policy, 10u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 2);
    if (evicted == 2) {
        PB_CHECK_U64(victims[0], 1);
        PB_CHECK_U64(victims[1], 2);
    }
    /* keptCount() is not on the C vtable; step skipped */

    pb_kvcache_pyramidkv.destroy(policy);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    case_4();
    case_5();
    return pb_test_summary("kv-cache/pyramidkv vectors");
}

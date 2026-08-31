/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for kv-cache/sliding-window, produced by scripts/gen-c-vectors.ts from
 * policies/kv-cache/sliding-window/vectors.json. Regenerate with:
 *
 *     pnpm gen:c-vectors
 *
 * The C implementation is conformant when it reproduces these results, which
 * are the same ones the TypeScript and Python implementations are held to.
 */

#include <stddef.h>
#include <stdint.h>

#include "policybook/kv_cache/kv_cache.h"
#include "policybook/kv_cache/sliding_window.h"
#include "policybook/rng.h"

#include "../pb_test.h"

#define VICTIM_CAPACITY 4

/* smoke: the oldest position goes first */
static void case_0(void)
{
    pb_rng rng;
    pb_kvcache_sliding_window_params params = PB_KVCACHE_SLIDING_WINDOW_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 4u;
    policy = pb_kvcache_sliding_window.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_sliding_window.on_decode_step(policy, 1u, NULL, 0);
    pb_kvcache_sliding_window.on_decode_step(policy, 2u, NULL, 0);
    pb_kvcache_sliding_window.on_decode_step(policy, 3u, NULL, 0);
    /* keptCount() is not on the C vtable; step skipped */
    pb_kvcache_sliding_window.on_decode_step(policy, 4u, NULL, 0);
    evicted = pb_kvcache_sliding_window.evict(policy, 4u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 0);
    }
    /* keptCount() is not on the C vtable; step skipped */
    pb_kvcache_sliding_window.on_decode_step(policy, 5u, NULL, 0);
    evicted = pb_kvcache_sliding_window.evict(policy, 4u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 1);
    }

    pb_kvcache_sliding_window.destroy(policy);
}

/* boundary: a budget of one keeps only the newest token */
static void case_1(void)
{
    pb_rng rng;
    pb_kvcache_sliding_window_params params = PB_KVCACHE_SLIDING_WINDOW_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 1u;
    policy = pb_kvcache_sliding_window.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_sliding_window.on_decode_step(policy, 1u, NULL, 0);
    evicted = pb_kvcache_sliding_window.evict(policy, 1u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 0);
    }
    pb_kvcache_sliding_window.on_decode_step(policy, 2u, NULL, 0);
    evicted = pb_kvcache_sliding_window.evict(policy, 1u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 1);
    }
    /* keptCount() is not on the C vtable; step skipped */

    pb_kvcache_sliding_window.destroy(policy);
}

/* distinguishing: the attention sinks are dropped first, not last */
static void case_2(void)
{
    pb_rng rng;
    pb_kvcache_sliding_window_params params = PB_KVCACHE_SLIDING_WINDOW_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 6u;
    policy = pb_kvcache_sliding_window.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_sliding_window.on_decode_step(policy, 1u, NULL, 0);
    pb_kvcache_sliding_window.on_decode_step(policy, 2u, NULL, 0);
    pb_kvcache_sliding_window.on_decode_step(policy, 3u, NULL, 0);
    pb_kvcache_sliding_window.on_decode_step(policy, 4u, NULL, 0);
    pb_kvcache_sliding_window.on_decode_step(policy, 5u, NULL, 0);
    /* keptCount() is not on the C vtable; step skipped */
    pb_kvcache_sliding_window.on_decode_step(policy, 6u, NULL, 0);
    evicted = pb_kvcache_sliding_window.evict(policy, 6u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 0);
    }
    pb_kvcache_sliding_window.on_decode_step(policy, 7u, NULL, 0);
    evicted = pb_kvcache_sliding_window.evict(policy, 6u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 1);
    }
    pb_kvcache_sliding_window.on_decode_step(policy, 8u, NULL, 0);
    evicted = pb_kvcache_sliding_window.evict(policy, 6u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 2);
    }

    pb_kvcache_sliding_window.destroy(policy);
}

/* tiebreak: eviction order is arrival order, oldest first */
static void case_3(void)
{
    pb_rng rng;
    pb_kvcache_sliding_window_params params = PB_KVCACHE_SLIDING_WINDOW_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 5u;
    policy = pb_kvcache_sliding_window.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_sliding_window.on_decode_step(policy, 1u, NULL, 0);
    pb_kvcache_sliding_window.on_decode_step(policy, 2u, NULL, 0);
    pb_kvcache_sliding_window.on_decode_step(policy, 3u, NULL, 0);
    pb_kvcache_sliding_window.on_decode_step(policy, 4u, NULL, 0);
    pb_kvcache_sliding_window.on_decode_step(policy, 5u, NULL, 0);
    evicted = pb_kvcache_sliding_window.evict(policy, 2u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 4);
    if (evicted == 4) {
        PB_CHECK_U64(victims[0], 0);
        PB_CHECK_U64(victims[1], 1);
        PB_CHECK_U64(victims[2], 2);
        PB_CHECK_U64(victims[3], 3);
    }
    /* keptCount() is not on the C vtable; step skipped */

    pb_kvcache_sliding_window.destroy(policy);
}

static const float attn_4_0[] = { 1.0f };
static const float attn_4_1[] = { 0.75f, 0.25f };
static const float attn_4_2[] = { 0.5f, 0.25f, 0.25f };
static const float attn_4_3[] = { 0.5f, 0.25f, 0.125f, 0.125f };

/* the attention is ignored, and passing it changes nothing */
static void case_4(void)
{
    pb_rng rng;
    pb_kvcache_sliding_window_params params = PB_KVCACHE_SLIDING_WINDOW_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 4u;
    policy = pb_kvcache_sliding_window.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_sliding_window.on_decode_step(policy, 1u, attn_4_0,
                             sizeof(attn_4_0) / sizeof(attn_4_0[0]));
    pb_kvcache_sliding_window.on_decode_step(policy, 2u, attn_4_1,
                             sizeof(attn_4_1) / sizeof(attn_4_1[0]));
    pb_kvcache_sliding_window.on_decode_step(policy, 3u, attn_4_2,
                             sizeof(attn_4_2) / sizeof(attn_4_2[0]));
    pb_kvcache_sliding_window.on_decode_step(policy, 4u, attn_4_3,
                             sizeof(attn_4_3) / sizeof(attn_4_3[0]));
    evicted = pb_kvcache_sliding_window.evict(policy, 4u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 0);
    }

    pb_kvcache_sliding_window.destroy(policy);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    case_4();
    return pb_test_summary("kv-cache/sliding-window vectors");
}

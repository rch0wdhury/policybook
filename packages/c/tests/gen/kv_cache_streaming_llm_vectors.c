/*
 * GENERATED FILE — do not edit.
 *
 * Vector test for kv-cache/streaming-llm, produced by scripts/gen-c-vectors.ts from
 * policies/kv-cache/streaming-llm/vectors.json. Regenerate with:
 *
 *     pnpm gen:c-vectors
 *
 * The C implementation is conformant when it reproduces these results, which
 * are the same ones the TypeScript and Python implementations are held to.
 */

#include <stddef.h>
#include <stdint.h>

#include "policybook/kv_cache/kv_cache.h"
#include "policybook/kv_cache/streaming_llm.h"
#include "policybook/rng.h"

#include "../pb_test.h"

#define VICTIM_CAPACITY 3

/* smoke: sinks are pinned and the window slides past them */
static void case_0(void)
{
    pb_rng rng;
    pb_kvcache_streaming_llm_params params = PB_KVCACHE_STREAMING_LLM_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 6u;
    params.sinks = 2u;
    policy = pb_kvcache_streaming_llm.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_streaming_llm.on_decode_step(policy, 1u, NULL, 0);
    pb_kvcache_streaming_llm.on_decode_step(policy, 2u, NULL, 0);
    pb_kvcache_streaming_llm.on_decode_step(policy, 3u, NULL, 0);
    pb_kvcache_streaming_llm.on_decode_step(policy, 4u, NULL, 0);
    pb_kvcache_streaming_llm.on_decode_step(policy, 5u, NULL, 0);
    /* keptCount() is not on the C vtable; step skipped */
    /* sinkCount() is not on the C vtable; step skipped */
    pb_kvcache_streaming_llm.on_decode_step(policy, 6u, NULL, 0);
    evicted = pb_kvcache_streaming_llm.evict(policy, 6u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 2);
    }
    pb_kvcache_streaming_llm.on_decode_step(policy, 7u, NULL, 0);
    evicted = pb_kvcache_streaming_llm.evict(policy, 6u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 3);
    }
    /* sinkCount() is not on the C vtable; step skipped */

    pb_kvcache_streaming_llm.destroy(policy);
}

/* boundary: one recency slot, the smallest coherent configuration */
static void case_1(void)
{
    pb_rng rng;
    pb_kvcache_streaming_llm_params params = PB_KVCACHE_STREAMING_LLM_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 3u;
    params.sinks = 2u;
    policy = pb_kvcache_streaming_llm.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_streaming_llm.on_decode_step(policy, 1u, NULL, 0);
    pb_kvcache_streaming_llm.on_decode_step(policy, 2u, NULL, 0);
    /* keptCount() is not on the C vtable; step skipped */
    pb_kvcache_streaming_llm.on_decode_step(policy, 3u, NULL, 0);
    evicted = pb_kvcache_streaming_llm.evict(policy, 3u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 2);
    }
    /* keptCount() is not on the C vtable; step skipped */

    pb_kvcache_streaming_llm.destroy(policy);
}

/* distinguishing: the sinks survive where a sliding window drops them */
static void case_2(void)
{
    pb_rng rng;
    pb_kvcache_streaming_llm_params params = PB_KVCACHE_STREAMING_LLM_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 6u;
    params.sinks = 4u;
    policy = pb_kvcache_streaming_llm.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_streaming_llm.on_decode_step(policy, 1u, NULL, 0);
    pb_kvcache_streaming_llm.on_decode_step(policy, 2u, NULL, 0);
    pb_kvcache_streaming_llm.on_decode_step(policy, 3u, NULL, 0);
    pb_kvcache_streaming_llm.on_decode_step(policy, 4u, NULL, 0);
    pb_kvcache_streaming_llm.on_decode_step(policy, 5u, NULL, 0);
    /* keptCount() is not on the C vtable; step skipped */
    pb_kvcache_streaming_llm.on_decode_step(policy, 6u, NULL, 0);
    evicted = pb_kvcache_streaming_llm.evict(policy, 6u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 4);
    }
    pb_kvcache_streaming_llm.on_decode_step(policy, 7u, NULL, 0);
    evicted = pb_kvcache_streaming_llm.evict(policy, 6u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 5);
    }
    pb_kvcache_streaming_llm.on_decode_step(policy, 8u, NULL, 0);
    evicted = pb_kvcache_streaming_llm.evict(policy, 6u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 6);
    }
    /* sinkCount() is not on the C vtable; step skipped */
    /* keptCount() is not on the C vtable; step skipped */

    pb_kvcache_streaming_llm.destroy(policy);
}

/* tiebreak: several victims come back oldest first */
static void case_3(void)
{
    pb_rng rng;
    pb_kvcache_streaming_llm_params params = PB_KVCACHE_STREAMING_LLM_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 6u;
    params.sinks = 2u;
    policy = pb_kvcache_streaming_llm.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_streaming_llm.on_decode_step(policy, 1u, NULL, 0);
    pb_kvcache_streaming_llm.on_decode_step(policy, 2u, NULL, 0);
    pb_kvcache_streaming_llm.on_decode_step(policy, 3u, NULL, 0);
    pb_kvcache_streaming_llm.on_decode_step(policy, 4u, NULL, 0);
    pb_kvcache_streaming_llm.on_decode_step(policy, 5u, NULL, 0);
    evicted = pb_kvcache_streaming_llm.evict(policy, 3u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 3);
    if (evicted == 3) {
        PB_CHECK_U64(victims[0], 2);
        PB_CHECK_U64(victims[1], 3);
        PB_CHECK_U64(victims[2], 4);
    }
    /* sinkCount() is not on the C vtable; step skipped */
    /* keptCount() is not on the C vtable; step skipped */

    pb_kvcache_streaming_llm.destroy(policy);
}

/* sinks: zero makes it a plain sliding window */
static void case_4(void)
{
    pb_rng rng;
    pb_kvcache_streaming_llm_params params = PB_KVCACHE_STREAMING_LLM_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 4u;
    params.sinks = 0u;
    policy = pb_kvcache_streaming_llm.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_streaming_llm.on_decode_step(policy, 1u, NULL, 0);
    pb_kvcache_streaming_llm.on_decode_step(policy, 2u, NULL, 0);
    pb_kvcache_streaming_llm.on_decode_step(policy, 3u, NULL, 0);
    /* sinkCount() is not on the C vtable; step skipped */
    pb_kvcache_streaming_llm.on_decode_step(policy, 4u, NULL, 0);
    evicted = pb_kvcache_streaming_llm.evict(policy, 4u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 0);
    }
    pb_kvcache_streaming_llm.on_decode_step(policy, 5u, NULL, 0);
    evicted = pb_kvcache_streaming_llm.evict(policy, 4u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 1);
    }

    pb_kvcache_streaming_llm.destroy(policy);
}

static const float attn_5_0[] = { 1.0f };
static const float attn_5_1[] = { 0.5f, 0.5f };
static const float attn_5_2[] = { 0.25f, 0.25f, 0.5f };
static const float attn_5_3[] = { 0.125f, 0.125f, 0.25f, 0.5f };
static const float attn_5_4[] = { 0.125f, 0.125f, 0.125f, 0.125f, 0.5f };
static const float attn_5_5[] = { 0.0625f, 0.0625f, 0.0625f, 0.0625f, 0.75f, 0.0625f };

/* the attention is ignored, and passing it changes nothing */
static void case_5(void)
{
    pb_rng rng;
    pb_kvcache_streaming_llm_params params = PB_KVCACHE_STREAMING_LLM_PARAMS_DEFAULT;
    pb_kvcache *policy;
    uint32_t victims[VICTIM_CAPACITY];
    size_t evicted;

    pb_rng_init(&rng, 1u);
    params.budget = 6u;
    params.sinks = 2u;
    policy = pb_kvcache_streaming_llm.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    pb_kvcache_streaming_llm.on_decode_step(policy, 1u, attn_5_0,
                             sizeof(attn_5_0) / sizeof(attn_5_0[0]));
    pb_kvcache_streaming_llm.on_decode_step(policy, 2u, attn_5_1,
                             sizeof(attn_5_1) / sizeof(attn_5_1[0]));
    pb_kvcache_streaming_llm.on_decode_step(policy, 3u, attn_5_2,
                             sizeof(attn_5_2) / sizeof(attn_5_2[0]));
    pb_kvcache_streaming_llm.on_decode_step(policy, 4u, attn_5_3,
                             sizeof(attn_5_3) / sizeof(attn_5_3[0]));
    pb_kvcache_streaming_llm.on_decode_step(policy, 5u, attn_5_4,
                             sizeof(attn_5_4) / sizeof(attn_5_4[0]));
    pb_kvcache_streaming_llm.on_decode_step(policy, 6u, attn_5_5,
                             sizeof(attn_5_5) / sizeof(attn_5_5[0]));
    evicted = pb_kvcache_streaming_llm.evict(policy, 6u, victims, VICTIM_CAPACITY);
    PB_CHECK_U64(evicted, 1);
    if (evicted == 1) {
        PB_CHECK_U64(victims[0], 2);
    }

    pb_kvcache_streaming_llm.destroy(policy);
}

int main(void)
{
    case_0();
    case_1();
    case_2();
    case_3();
    case_4();
    case_5();
    return pb_test_summary("kv-cache/streaming-llm vectors");
}

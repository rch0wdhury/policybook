/*
 * What the shared vectors cannot check about the C kv-caches: create-time
 * bounds.
 *
 * Every policy here sizes at least one table at budget + 1 slots, so a budget
 * of UINT32_MAX would wrap the capacity to zero and the constructor would
 * write position 0 into an empty allocation. The float64 reference has no
 * such edge, so the refusal is C's own (concept.md §12.2) and needs its own
 * test.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "policybook/kv_cache/h2o.h"
#include "policybook/kv_cache/kv_cache.h"
#include "policybook/kv_cache/pyramidkv.h"
#include "policybook/kv_cache/scissorhands.h"
#include "policybook/kv_cache/snapkv.h"
#include "policybook/kv_cache/sliding_window.h"
#include "policybook/kv_cache/streaming_llm.h"
#include "policybook/kv_cache/tova.h"

#include "pb_test.h"

typedef struct kvcache_case {
    const char *name;
    const pb_kvcache_vtable *vtable;
    const void *max_budget_params;
    const void *small_params;
} kvcache_case;

static const pb_kvcache_sliding_window_params sliding_window_max = { UINT32_MAX };
static const pb_kvcache_sliding_window_params sliding_window_small = { 8u };
static const pb_kvcache_tova_params tova_max = { UINT32_MAX };
static const pb_kvcache_tova_params tova_small = { 8u };
static const pb_kvcache_h2o_params h2o_max = { UINT32_MAX, 4u };
static const pb_kvcache_h2o_params h2o_small = { 8u, 4u };
static const pb_kvcache_scissorhands_params scissorhands_max = { UINT32_MAX, 4u };
static const pb_kvcache_scissorhands_params scissorhands_small = { 8u, 4u };
static const pb_kvcache_snapkv_params snapkv_max = { UINT32_MAX, 4u, 2u, 3u };
static const pb_kvcache_snapkv_params snapkv_small = { 8u, 4u, 2u, 3u };
static const pb_kvcache_pyramidkv_params pyramidkv_max = { UINT32_MAX, 0u, 1u, 4u, 4u, 2u, 3u };
static const pb_kvcache_pyramidkv_params pyramidkv_small = { 8u, 0u, 1u, 4u, 4u, 2u, 3u };
/* Zero sinks is the wrapping path: capacity is budget - sinks + 1. */
static const pb_kvcache_streaming_llm_params streaming_llm_max = { UINT32_MAX, 0u };
static const pb_kvcache_streaming_llm_params streaming_llm_small = { 8u, 2u };

static const kvcache_case CASES[] = {
    { "sliding-window", &pb_kvcache_sliding_window, &sliding_window_max,
      &sliding_window_small },
    { "tova", &pb_kvcache_tova, &tova_max, &tova_small },
    { "h2o", &pb_kvcache_h2o, &h2o_max, &h2o_small },
    { "scissorhands", &pb_kvcache_scissorhands, &scissorhands_max, &scissorhands_small },
    { "snapkv", &pb_kvcache_snapkv, &snapkv_max, &snapkv_small },
    { "pyramidkv", &pb_kvcache_pyramidkv, &pyramidkv_max, &pyramidkv_small },
    { "streaming-llm", &pb_kvcache_streaming_llm, &streaming_llm_max,
      &streaming_llm_small }
};

#define CASE_COUNT (sizeof(CASES) / sizeof(CASES[0]))

/* A budget of UINT32_MAX is refused; the same shape one power of two down is
 * not, so the guard rejects only the budget that would wrap. */
static void test_budget_uint32_max_is_refused(void)
{
    size_t i;

    for (i = 0; i < CASE_COUNT; ++i) {
        const pb_kvcache_vtable *v = CASES[i].vtable;
        pb_kvcache *policy = v->create(CASES[i].max_budget_params, NULL, NULL);

        PB_CHECK(policy == NULL);
        if (policy != NULL) {
            v->destroy(policy);
        }

        policy = v->create(CASES[i].small_params, NULL, NULL);
        PB_CHECK(policy != NULL);
        v->destroy(policy);
    }
}

int main(void)
{
    test_budget_uint32_max_is_refused();
    return pb_test_summary("kv-cache C behaviour");
}

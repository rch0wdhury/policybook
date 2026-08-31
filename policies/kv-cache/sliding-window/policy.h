/*
 * Sliding window — keep the most recent tokens and forget the rest.
 *
 * The baseline every other policy in this domain is measured against, and a
 * much stronger one than it looks: attention is dominated by recency. What it
 * gets wrong is everything old and still important — above all the attention
 * sinks at the start of the sequence, which is the whole of streaming_llm.
 *
 *     #include <policybook/kv_cache/kv_cache.h>
 *     #include <policybook/kv_cache/sliding_window.h>
 *
 *     pb_kvcache_sliding_window_params params =
 *         PB_KVCACHE_SLIDING_WINDOW_PARAMS_DEFAULT;
 *     params.budget = 512u;
 *
 *     pb_kvcache *policy = pb_kvcache_sliding_window.create(&params, NULL, NULL);
 *     pb_kvcache_sliding_window.on_decode_step(policy, pos, NULL, 0);
 *     size_t n = pb_kvcache_sliding_window.evict(policy, 512u, victims, cap);
 *     pb_kvcache_sliding_window.destroy(policy);
 *
 * `rng` may be NULL: this policy is entirely deterministic. `attn` may be NULL
 * too, and is ignored in any case.
 *
 * Memory: the struct plus `(budget + 1) * sizeof(uint32_t)` for the ring — one
 * slot more than the budget, because that is the most that can be held before
 * an eviction is asked for. Nothing is allocated after create.
 */

#ifndef POLICYBOOK_KV_CACHE_SLIDING_WINDOW_H
#define POLICYBOOK_KV_CACHE_SLIDING_WINDOW_H

#include <stdint.h>

#include "policybook/kv_cache/kv_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_kvcache_sliding_window_params {
    uint32_t budget; /* maximum token positions kept */
} pb_kvcache_sliding_window_params;

#define PB_KVCACHE_SLIDING_WINDOW_PARAMS_DEFAULT { 512u }

extern const pb_kvcache_vtable pb_kvcache_sliding_window;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_KV_CACHE_SLIDING_WINDOW_H */

/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/streaming-llm/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * StreamingLLM — a sliding window that also pins the first few tokens.
 *
 * A large, roughly content-independent share of every attention distribution
 * lands on the first few tokens of a sequence. Xiao et al. called them
 * attention sinks; evicting them degrades generation immediately rather than
 * gradually, which is exactly what a plain sliding window does since they are
 * the oldest thing it holds.
 *
 *     #include <policybook/kv_cache/kv_cache.h>
 *     #include <policybook/kv_cache/streaming_llm.h>
 *
 *     pb_kvcache_streaming_llm_params params =
 *         PB_KVCACHE_STREAMING_LLM_PARAMS_DEFAULT;
 *     params.budget = 512u;
 *
 *     pb_kvcache *policy = pb_kvcache_streaming_llm.create(&params, NULL, NULL);
 *     pb_kvcache_streaming_llm.on_decode_step(policy, pos, NULL, 0);
 *     size_t n = pb_kvcache_streaming_llm.evict(policy, 512u, victims, cap);
 *     pb_kvcache_streaming_llm.destroy(policy);
 *
 * `rng` may be NULL: this policy is entirely deterministic. `attn` may be NULL
 * too, and is ignored in any case — it pins the structurally special positions,
 * not the important ones.
 *
 * `create` returns NULL if `sinks >= budget`, which would leave no room for a
 * recency window.
 *
 * Memory: the struct plus `(budget - sinks + 1) * sizeof(uint32_t)` for the
 * window ring. The sinks themselves cost nothing to track — they are the
 * positions below `sinks`, so a count suffices. Nothing is allocated after
 * create.
 */

#ifndef POLICYBOOK_KV_CACHE_STREAMING_LLM_H
#define POLICYBOOK_KV_CACHE_STREAMING_LLM_H

#include <stdint.h>

#include "policybook/kv_cache/kv_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_kvcache_streaming_llm_params {
    uint32_t budget; /* maximum token positions kept */
    uint32_t sinks;  /* how many of the first positions are pinned */
} pb_kvcache_streaming_llm_params;

#define PB_KVCACHE_STREAMING_LLM_PARAMS_DEFAULT { 512u, 4u }

extern const pb_kvcache_vtable pb_kvcache_streaming_llm;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_KV_CACHE_STREAMING_LLM_H */

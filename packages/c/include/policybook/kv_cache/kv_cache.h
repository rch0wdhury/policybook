/*
 * The `kv-cache` domain: which tokens to forget during LLM decoding.
 *
 * A transformer's KV cache grows by one entry per token, per layer, per head.
 * At a 4,096-token context that is gigabytes, and the cost is linear in the
 * sequence while the value of any individual token is not — most attention
 * lands on a handful of positions. Dropping the rest is what makes long
 * contexts affordable, and *which* to drop is this domain.
 *
 * This is the domain where the C implementation earns its keep: the policy runs
 * inside an inference server's per-step budget, so it must not allocate, and it
 * sees only attention weights — never the tensors themselves.
 *
 * Every policy exports a `const pb_kvcache_vtable` and a params struct with a
 * _DEFAULT initialiser:
 *
 *     #include <policybook/kv_cache/kv_cache.h>
 *     #include <policybook/kv_cache/streaming_llm.h>
 *
 *     pb_kvcache_streaming_llm_params params =
 *         PB_KVCACHE_STREAMING_LLM_PARAMS_DEFAULT;
 *     params.budget = 512u;
 *
 *     pb_kvcache *policy = pb_kvcache_streaming_llm.create(&params, NULL, NULL);
 *     pb_kvcache_streaming_llm.on_decode_step(policy, pos, attn, attn_len);
 *     if (kept > params.budget) {
 *         size_t dropped = pb_kvcache_streaming_llm.evict(
 *             policy, params.budget, victims, capacity);
 *     }
 *     pb_kvcache_streaming_llm.destroy(policy);
 *
 * **Positions are absolute token indices**, not offsets into the kept set. A
 * policy that renumbered on eviction could say nothing about where a token sits
 * in the sequence, and nearly every policy here cares: attention sinks are the
 * *first* few positions, recency windows are the *last* few.
 */

#ifndef POLICYBOOK_KV_CACHE_KV_CACHE_H
#define POLICYBOOK_KV_CACHE_KV_CACHE_H

#include <stddef.h>
#include <stdint.h>

#include "policybook/allocator.h"
#include "policybook/rng.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque per-policy state. */
typedef struct pb_kvcache pb_kvcache;

/*
 * The budgets every canonical benchmark runs.
 *
 * A 4,096-token sequence held at 256, 512 and 1,024 — from a sixteenth of the
 * context to a quarter. Below a sixteenth every policy is mostly noise; above a
 * quarter almost nothing is evicted and they all look alike.
 */
#define PB_KVCACHE_BUDGET_SMALL 256u
#define PB_KVCACHE_BUDGET_MEDIUM 512u
#define PB_KVCACHE_BUDGET_LARGE 1024u

typedef struct pb_kvcache_vtable {
    /*
     * Allocate the policy and everything it will ever need.
     *
     * Everything is allocated here and nothing after, which is binding in this
     * domain rather than merely encouraged: a policy that
     * called malloc on the decode path would add an unbounded pause to every
     * token. That means `budget` must be known at create, and the eviction
     * buffer sized from it.
     *
     * `rng` may be NULL for a deterministic policy, which every policy in this
     * domain is so far.
     */
    pb_kvcache *(*create)(const void *params, const pb_allocator *allocator, pb_rng *rng);

    /*
     * Called once per decode step, before any eviction.
     *
     * `pos` is the position of the token being generated. `attn` holds the
     * attention weights the policy's kept positions received, in ascending
     * position order — so `attn[i]` belongs to the i-th position the policy
     * still holds, and the policy is expected to know its own kept order.
     * `attn_len` is how many, which is exactly the number of positions held.
     *
     * **The kept set starts as {0}.** Position 0's token exists before the
     * first decode step, so the first call is `on_decode_step(policy, 1, attn,
     * 1)` and a policy must initialise its bookkeeping with position 0 held.
     *
     * `attn` may be NULL for a policy that does not read attention at all (a
     * sliding window does not), in which case `attn_len` is 0.
     *
     * **The weights are not renormalised** after eviction. They are the model's
     * own attention over the full sequence, restricted to what the policy kept,
     * so they sum to less than one by exactly the mass already discarded.
     */
    void (*on_decode_step)(pb_kvcache *policy, uint32_t pos, const float *attn,
                           size_t attn_len);

    /*
     * Name positions to drop, writing them into `victims`.
     *
     * Returns how many were written. The returned positions must currently be
     * held, and removing them must bring the kept set to `budget` or below.
     * `capacity` is how many `victims` can take; a policy that would exceed it
     * writes nothing and returns 0, which the harness treats as a failure to
     * evict rather than working around.
     */
    size_t (*evict)(pb_kvcache *policy, uint32_t budget, uint32_t *victims, size_t capacity);

    /* Bytes held by the policy, for the memory column. May be NULL. */
    size_t (*memory_bytes)(const pb_kvcache *policy);

    /* Release everything `create` took. Safe on NULL. */
    void (*destroy)(pb_kvcache *policy);
} pb_kvcache_vtable;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_KV_CACHE_KV_CACHE_H */

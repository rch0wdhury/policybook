/*
 * SnapKV — score on the last few steps, then max-pool across neighbours.
 *
 * Two ideas. A forgetting window: the score is the attention a position
 * received over the last obs_window steps only. And a max-pool across
 * positions: before choosing victims, each score is replaced by the maximum
 * over its pool_kernel neighbours in kept order, so a high scorer defends the
 * tokens around it rather than leaving a fragment of a phrase behind.
 *
 *     #include <policybook/kv_cache/kv_cache.h>
 *     #include <policybook/kv_cache/snapkv.h>
 *
 *     pb_kvcache_snapkv_params params = PB_KVCACHE_SNAPKV_PARAMS_DEFAULT;
 *     params.budget = 512u;
 *
 *     pb_kvcache *policy = pb_kvcache_snapkv.create(&params, NULL, NULL);
 *     pb_kvcache_snapkv.on_decode_step(policy, pos, attn, attn_len);
 *     size_t n = pb_kvcache_snapkv.evict(policy, 512u, victims, cap);
 *     pb_kvcache_snapkv.destroy(policy);
 *
 * `rng` may be NULL: this policy is entirely deterministic. `attn` may be NULL,
 * in which case the call is entirely inert: nothing is recorded and the ring
 * does not advance, so the window spans the last obs_window *observed* steps.
 * Advancing without writing would leave a stale weight in the slot for another
 * full cycle.
 *
 * `create` returns NULL if `recent_window >= budget`, if `obs_window` is zero,
 * or if `pool_kernel` is even or zero. An even kernel has no centre, so the
 * neighbourhood would be lopsided and the pooling would drift one way.
 *
 * Memory: the struct plus, per slot, 4 bytes of position, `obs_window` floats
 * of history, two doubles and a byte of eviction scratch — 4 + 4*obs_window +
 * 17 bytes, so about 45 KB at the defaults. This is much the heaviest policy in
 * the domain, and the history is what costs it. Nothing is allocated after
 * create.
 */

#ifndef POLICYBOOK_KV_CACHE_SNAPKV_H
#define POLICYBOOK_KV_CACHE_SNAPKV_H

#include <stdint.h>

#include "policybook/kv_cache/kv_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_kvcache_snapkv_params {
    uint32_t budget;        /* maximum token positions kept */
    uint32_t recent_window; /* newest positions, never evicted on score */
    uint32_t obs_window;    /* decode steps the score is summed over */
    uint32_t pool_kernel;   /* max-pool width across neighbours; must be odd */
} pb_kvcache_snapkv_params;

#define PB_KVCACHE_SNAPKV_PARAMS_DEFAULT { 512u, 32u, 16u, 7u }

extern const pb_kvcache_vtable pb_kvcache_snapkv;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_KV_CACHE_SNAPKV_H */

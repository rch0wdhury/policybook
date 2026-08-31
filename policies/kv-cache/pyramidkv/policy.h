/*
 * PyramidKV — spend more cache on early layers than late ones.
 *
 * Every other policy in this domain decides which tokens to keep. This one
 * decides how many, and leaves the choosing to a SnapKV-style rule underneath.
 *
 * Attention in the early layers of a transformer is broad and fairly uniform;
 * in later layers it concentrates onto a few positions. A uniform per-layer
 * budget therefore overfeeds the deep layers and starves the shallow ones, so
 * the budget is redistributed as an arithmetic sequence across layers, with the
 * first getting `pyramid_ratio` times the last and the average preserved.
 *
 *     #include <policybook/kv_cache/kv_cache.h>
 *     #include <policybook/kv_cache/pyramidkv.h>
 *
 *     pb_kvcache_pyramidkv_params params = PB_KVCACHE_PYRAMIDKV_PARAMS_DEFAULT;
 *     params.budget = 512u;
 *     params.num_layers = 32u;
 *     params.layer = 7u;
 *
 *     pb_kvcache *policy = pb_kvcache_pyramidkv.create(&params, NULL, NULL);
 *     pb_kvcache_pyramidkv.on_decode_step(policy, pos, attn, attn_len);
 *     size_t n = pb_kvcache_pyramidkv.evict(policy, 512u, victims, cap);
 *     pb_kvcache_pyramidkv.destroy(policy);
 *
 * One instance serves one layer. A real deployment builds `num_layers` of them
 * and gives each its own `layer`; `pb_kvcache_pyramid_budget` computes a
 * layer's share without constructing anything, for callers sizing buffers up
 * front.
 *
 * With `num_layers == 1` the allocation returns `budget` unchanged and this is
 * exactly SnapKV.
 *
 * `rng` may be NULL: this policy is entirely deterministic. `attn` may be NULL,
 * in which case the call is inert, ring included.
 *
 * `create` returns NULL if `layer >= num_layers`, if `pyramid_ratio` is zero,
 * if `recent_window >= budget`, if `obs_window` is zero, or if `pool_kernel` is
 * even or zero.
 *
 * Memory: as snapkv, sized from whichever of `budget` and this layer's share is
 * larger — a shallow layer's share exceeds the average by design.
 */

#ifndef POLICYBOOK_KV_CACHE_PYRAMIDKV_H
#define POLICYBOOK_KV_CACHE_PYRAMIDKV_H

#include <stdint.h>

#include "policybook/kv_cache/kv_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_kvcache_pyramidkv_params {
    uint32_t budget;        /* average positions kept per layer */
    uint32_t layer;         /* which layer this instance serves */
    uint32_t num_layers;    /* layers the budget is shared across */
    uint32_t pyramid_ratio; /* first layer's share over the last layer's */
    uint32_t recent_window; /* newest positions, never evicted on score */
    uint32_t obs_window;    /* decode steps the score is summed over */
    uint32_t pool_kernel;   /* max-pool width across neighbours; must be odd */
} pb_kvcache_pyramidkv_params;

#define PB_KVCACHE_PYRAMIDKV_PARAMS_DEFAULT { 512u, 0u, 1u, 4u, 32u, 16u, 7u }

/*
 * How much cache `layer` gets when `num_layers` share `budget` on average.
 *
 * An arithmetic sequence from 2*budget*r/(r+1) down to 2*budget/(r+1), whose
 * mean is `budget` by construction, evaluated as a single integer division so
 * that all three implementations floor at the same point:
 *
 *     effective(k) = 2*budget*( r*(L-1) - k*(r-1) ) / ( (r+1)*(L-1) )
 *
 * Computed in uint64_t: the numerator reaches 2*budget*ratio*num_layers, which
 * overflows 32 bits for a large model at a large budget.
 *
 * Returns `budget` when `num_layers` is 1 — there is nothing to redistribute,
 * and the denominator would be zero.
 */
uint32_t pb_kvcache_pyramid_budget(uint32_t budget, uint32_t layer, uint32_t num_layers,
                                   uint32_t pyramid_ratio);

extern const pb_kvcache_vtable pb_kvcache_pyramidkv;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_KV_CACHE_PYRAMIDKV_H */

/*
 * H2O — keep the tokens that have received the most attention so far.
 *
 * The first policy in this domain that reads the attention weights. Attention
 * is not merely sparse but persistently sparse: a small set of positions
 * accumulates most of it across a generation, and which positions those are is
 * fairly stable. Sum each position's attention over time, and evict the
 * smallest sum.
 *
 *     #include <policybook/kv_cache/kv_cache.h>
 *     #include <policybook/kv_cache/h2o.h>
 *
 *     pb_kvcache_h2o_params params = PB_KVCACHE_H2O_PARAMS_DEFAULT;
 *     params.budget = 512u;
 *
 *     pb_kvcache *policy = pb_kvcache_h2o.create(&params, NULL, NULL);
 *     pb_kvcache_h2o.on_decode_step(policy, pos, attn, attn_len);
 *     size_t n = pb_kvcache_h2o.evict(policy, 512u, victims, cap);
 *     pb_kvcache_h2o.destroy(policy);
 *
 * `rng` may be NULL: this policy is entirely deterministic. `attn` may be NULL,
 * which the policy reads as "no information this step" and leaves every score
 * unchanged.
 *
 * `create` returns NULL if `recent_window >= budget`, which would leave the
 * score nothing to choose between.
 *
 * Scores accumulate in `double` while the weights arrive as `float`. That is
 * deliberate and matches the other two implementations: accumulating 4,000
 * float32 additions would lose low-order bits that the comparison then depends
 * on.
 *
 * Memory: the struct plus `(budget + 1)` each of `uint32_t` positions, `double`
 * scores and one byte of eviction scratch — 13 bytes per slot, so about 6.7 KB
 * at the default budget. Nothing is allocated after create.
 */

#ifndef POLICYBOOK_KV_CACHE_H2O_H
#define POLICYBOOK_KV_CACHE_H2O_H

#include <stdint.h>

#include "policybook/kv_cache/kv_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_kvcache_h2o_params {
    uint32_t budget;        /* maximum token positions kept */
    uint32_t recent_window; /* newest positions, never evicted on score */
} pb_kvcache_h2o_params;

#define PB_KVCACHE_H2O_PARAMS_DEFAULT { 512u, 32u }

extern const pb_kvcache_vtable pb_kvcache_h2o;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_KV_CACHE_H2O_H */

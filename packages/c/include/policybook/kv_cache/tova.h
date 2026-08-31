/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/tova/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * TOVA — drop whichever token the model just stopped looking at.
 *
 * A decoder-only transformer is really a multi-state RNN whose state is the KV
 * cache, and at each step the model tells you through its attention which
 * states it is using. Drop the least-used one. There is no accumulation, so
 * nothing a position did earlier defends it.
 *
 *     #include <policybook/kv_cache/kv_cache.h>
 *     #include <policybook/kv_cache/tova.h>
 *
 *     pb_kvcache_tova_params params = PB_KVCACHE_TOVA_PARAMS_DEFAULT;
 *     params.budget = 512u;
 *
 *     pb_kvcache *policy = pb_kvcache_tova.create(&params, NULL, NULL);
 *     pb_kvcache_tova.on_decode_step(policy, pos, attn, attn_len);
 *     size_t n = pb_kvcache_tova.evict(policy, 512u, victims, cap);
 *     pb_kvcache_tova.destroy(policy);
 *
 * `rng` may be NULL: this policy is entirely deterministic. `attn` may be NULL,
 * in which case no position's record is updated.
 *
 * There is no recent-window parameter, unlike every other scoring policy here.
 * Recency is already in the signal — recent tokens attract high attention now.
 * The token just generated is admitted as *unobserved* and is not a candidate
 * for eviction until something has attended to it, which is a refusal to rank
 * it on evidence that does not exist rather than a recency rule.
 *
 * Memory: the struct plus `(budget + 1)` each of `uint32_t` positions, `double`
 * last-attention and one byte of eviction scratch — 13 bytes per slot, so about
 * 6.7 KB at the default budget. Nothing is allocated after create.
 */

#ifndef POLICYBOOK_KV_CACHE_TOVA_H
#define POLICYBOOK_KV_CACHE_TOVA_H

#include <stdint.h>

#include "policybook/kv_cache/kv_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_kvcache_tova_params {
    uint32_t budget; /* maximum token positions kept */
} pb_kvcache_tova_params;

#define PB_KVCACHE_TOVA_PARAMS_DEFAULT { 512u }

extern const pb_kvcache_vtable pb_kvcache_tova;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_KV_CACHE_TOVA_H */

/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/scissorhands/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * Scissorhands — count how many steps a token mattered for, not how much.
 *
 * The persistence of importance hypothesis: a token influential at one decoding
 * step tends to stay influential. So a position earns a vote on every step where
 * its attention exceeds its fair share — 1 / kept — and the fewest votes is
 * what gets evicted.
 *
 *     #include <policybook/kv_cache/kv_cache.h>
 *     #include <policybook/kv_cache/scissorhands.h>
 *
 *     pb_kvcache_scissorhands_params params =
 *         PB_KVCACHE_SCISSORHANDS_PARAMS_DEFAULT;
 *     params.budget = 512u;
 *
 *     pb_kvcache *policy = pb_kvcache_scissorhands.create(&params, NULL, NULL);
 *     pb_kvcache_scissorhands.on_decode_step(policy, pos, attn, attn_len);
 *     size_t n = pb_kvcache_scissorhands.evict(policy, 512u, victims, cap);
 *     pb_kvcache_scissorhands.destroy(policy);
 *
 * `rng` may be NULL: this policy is entirely deterministic. `attn` may be NULL,
 * which the policy reads as "no information this step" and leaves every vote
 * count unchanged.
 *
 * `create` returns NULL if `recent_window >= budget`, which would leave the
 * votes nothing to choose between.
 *
 * The share comparison is `attn[i] > 1.0 / attn_len` in `double`, strictly —
 * a position that exactly matches its share does not vote. That strictness is
 * pinned by a vector, because "exceeds" and "at least" are exactly the kind of
 * difference that would otherwise diverge quietly between ports.
 *
 * Memory: the struct plus `(budget + 1)` each of `uint32_t` positions,
 * `uint32_t` votes and one byte of eviction scratch — 9 bytes per slot, so
 * about 4.6 KB at the default budget, and rather less than h2o's float64
 * scores. Nothing is allocated after create.
 */

#ifndef POLICYBOOK_KV_CACHE_SCISSORHANDS_H
#define POLICYBOOK_KV_CACHE_SCISSORHANDS_H

#include <stdint.h>

#include "policybook/kv_cache/kv_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_kvcache_scissorhands_params {
    uint32_t budget;        /* maximum token positions kept */
    uint32_t recent_window; /* newest positions, never evicted on votes */
} pb_kvcache_scissorhands_params;

#define PB_KVCACHE_SCISSORHANDS_PARAMS_DEFAULT { 512u, 32u }

extern const pb_kvcache_vtable pb_kvcache_scissorhands;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_KV_CACHE_SCISSORHANDS_H */

/*
 * The kv-cache fuzzer's body, shared by two drivers.
 *
 * `fuzz_kvcache.c` feeds it coverage-guided input under libFuzzer, which is what
 * finds the strange sequences. `fuzz_kvcache_standalone.c` feeds it
 * pseudo-random input and runs as an ordinary test, so the invariants below are
 * checked on every build rather than only when someone remembers to fuzz — and
 * on machines whose Clang has no fuzzer runtime.
 *
 * The invariants are the contract every kv-cache policy claims to keep:
 *
 *   - after `evict`, the kept set is within the budget. This is the whole point
 *     of the domain: a policy that quietly held more would defeat the memory
 *     bound it exists to enforce;
 *   - every returned victim is a position the policy currently holds, in range,
 *     and named at most once. A duplicate would make the caller's accounting
 *     drift silently;
 *   - `evict` frees enough to reach the budget, or the caller is left over
 *     budget with nothing to do about it;
 *   - **no allocation happens after `create`**, checked by counting calls
 *     through a `pb_allocator` the fuzzer supplies rather than by inspecting
 *     `memory_bytes`. That is the stronger of the two: a policy could free and
 *     reallocate the same number of bytes on the decode path and keep
 *     `memory_bytes` constant while still calling malloc per token, which is
 *     exactly the pause a KV cache cannot afford (concept.md §12.2);
 *   - `memory_bytes` does not change after `create` either, which is the
 *     weaker check kept for the same reason the other domains keep it;
 *   - nothing reads or writes out of bounds, which AddressSanitizer decides.
 *
 * The fuzzer mirrors the TypeScript harness exactly: the cache starts holding
 * position 0, `on_decode_step` is called before the new position joins, and
 * `evict` is called only once the kept set has passed the budget.
 *
 * Attention weights are synthesised from the input bytes and are always
 * non-negative — a negative weight is not something a softmax can produce, and
 * TOVA reserves a negative value as its "never observed" sentinel.
 */

#ifndef POLICYBOOK_FUZZ_KVCACHE_CORE_H
#define POLICYBOOK_FUZZ_KVCACHE_CORE_H

#include <stddef.h>
#include <stdint.h>

/* Budgets stay small so eviction pressure is constant rather than incidental. */
#define PB_FUZZ_KV_MAX_BUDGET 64u
/* Positions never exceed this, so the shadow bookkeeping can be a flat array. */
#define PB_FUZZ_KV_MAX_STEPS 256u

/*
 * Drive one policy through a sequence decoded from `data`.
 *
 * Returns the number of invariant violations seen, which is always zero unless
 * something is wrong. The libFuzzer driver aborts on a non-zero result; the
 * standalone driver reports it.
 */
int pb_fuzz_kvcache_once(const uint8_t *data, size_t size);

/* How many policies the fuzzer knows about, for the standalone driver's report. */
unsigned pb_fuzz_kvcache_policy_count(void);

#endif /* POLICYBOOK_FUZZ_KVCACHE_CORE_H */

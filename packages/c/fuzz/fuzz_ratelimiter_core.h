/*
 * The rate-limiter fuzzer's body, shared by two drivers.
 *
 * `fuzz_ratelimiter.c` feeds it coverage-guided input under libFuzzer, which is
 * what finds the strange sequences. `fuzz_ratelimiter_standalone.c` feeds it
 * pseudo-random input and runs as an ordinary test, so the invariants below are
 * checked on every build rather than only when someone remembers to fuzz — and
 * on machines whose Clang has no fuzzer runtime.
 *
 * The invariants are the contract every rate-limiter policy claims to keep:
 *
 *   - `allow` is a pure function of the policy's state, so asking twice at the
 *     same instant after an identical history gives the same answer;
 *   - `retry_after` is **honest**: with one key and nothing else arriving,
 *     advancing the clock by exactly that many milliseconds makes the next
 *     `allow` succeed. A hint nobody can act on is worse than none;
 *   - `retry_after` returns zero when the policy would admit right now;
 *   - `state_size` never exceeds `max_keys`, which is what the fail-closed
 *     bound is for;
 *   - `memory_bytes` does not change after `create`, which is the
 *     no-allocation-after-create rule of;
 *   - nothing reads or writes out of bounds, which AddressSanitizer decides.
 *
 * Time only ever moves forward, because the interface says `now_ms` is
 * non-decreasing and a policy handed a clock that went backwards is entitled to
 * do anything at all.
 */

#ifndef POLICYBOOK_FUZZ_RATELIMITER_CORE_H
#define POLICYBOOK_FUZZ_RATELIMITER_CORE_H

#include <stddef.h>
#include <stdint.h>

/* Keys are kept in a small universe so reuse and eviction pressure are common. */
#define PB_FUZZ_RL_KEY_SPACE 8u

/*
 * Drive one policy through a sequence decoded from `data`.
 *
 * Returns the number of invariant violations seen, which is always zero unless
 * something is wrong. The libFuzzer driver aborts on a non-zero result; the
 * standalone driver reports it.
 */
int pb_fuzz_ratelimiter_once(const uint8_t *data, size_t size);

/* How many policies the fuzzer knows about, for the standalone driver's report. */
unsigned pb_fuzz_ratelimiter_policy_count(void);

#endif /* POLICYBOOK_FUZZ_RATELIMITER_CORE_H */

/*
 * The cache fuzzer's body, shared by two drivers.
 *
 * `fuzz_cache.c` feeds it coverage-guided input under libFuzzer, which is what
 * finds the strange sequences. `fuzz_cache_standalone.c` feeds it pseudo-random
 * input and runs as an ordinary test, so the invariants below are checked on
 * every build rather than only when someone remembers to fuzz — and on machines
 * whose Clang has no fuzzer runtime.
 *
 * The invariants are the contract every cache policy claims to keep:
 *
 *   - `evict` returns a key the caller currently holds, never a stale or
 *     invented one;
 *   - the resident count never exceeds the capacity once evictions are done;
 *   - `memory_bytes` does not change after `create`, which is the
 *     no-allocation-after-create rule of;
 *   - nothing reads or writes out of bounds, which AddressSanitizer decides.
 */

#ifndef POLICYBOOK_FUZZ_CACHE_CORE_H
#define POLICYBOOK_FUZZ_CACHE_CORE_H

#include <stddef.h>
#include <stdint.h>

/* Keys are kept in a small universe so reuse and collisions are common. */
#define PB_FUZZ_KEY_SPACE 256u

/*
 * Drive one policy through a sequence decoded from `data`.
 *
 * Returns the number of invariant violations seen, which is always zero unless
 * something is wrong. The libFuzzer driver aborts on a non-zero result; the
 * standalone driver reports it.
 */
int pb_fuzz_cache_once(const uint8_t *data, size_t size);

/* How many policies the fuzzer knows about, for the standalone driver's report. */
unsigned pb_fuzz_cache_policy_count(void);

#endif /* POLICYBOOK_FUZZ_CACHE_CORE_H */

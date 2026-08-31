/*
 * GENERATED COPY — do not edit. Edit policies/cache/sieve/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * SIEVE — a FIFO queue with a hand that gives each entry one chance.
 *
 * The same shape as CLOCK, with one difference that changes everything: a
 * survivor is not moved to the back. It stays where it is, and the hand stays
 * where it stopped. Old entries keep having to earn their place; new entries
 * face the hand before travelling the whole queue, so one-hit wonders die young.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/sieve.h>
 *
 *     pb_cache_sieve_params params = PB_CACHE_SIEVE_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_sieve.create(&params, NULL, NULL);
 *
 * Memory: about 48 bytes per entry — 8 for the key, 1 for the visited bit, 8
 * for the two order links, 4 for the free stack, and roughly 27 for the hash
 * table. Call `memory_bytes` for the exact figure.
 *
 * As with CLOCK, a single eviction may sweep every entry when all bits are set:
 * O(1) amortised, O(n) worst case for one call.
 */

#ifndef POLICYBOOK_CACHE_SIEVE_H
#define POLICYBOOK_CACHE_SIEVE_H

#include <stdint.h>

#include "policybook/cache/cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_sieve_params {
    uint32_t capacity; /* maximum number of entries held */
} pb_cache_sieve_params;

#define PB_CACHE_SIEVE_PARAMS_DEFAULT { 1000u }

extern const pb_cache_vtable pb_cache_sieve;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_SIEVE_H */

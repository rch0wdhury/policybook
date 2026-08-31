/*
 * SIEVE over the canonical Zipf trace — what a cache policy looks like in use.
 *
 * Build (against the installed library):
 *     cc -std=c99 cache_sieve.c -lpolicybook -lm -o cache_sieve
 *
 * Build (against the single header, no build system at all):
 *     cc -std=c99 -DPOLICYBOOK_IMPLEMENTATION -I../dist cache_sieve.c -lm -o cache_sieve
 *
 * The whole interface is a handful of function pointers on a vtable. A policy
 * decides *which* key to drop; the caller owns the storage and does the
 * dropping. That split is what lets the same policy run in a test harness, in
 * this program, and in front of a real map without changing.
 */

#include <stdio.h>
#include <stdlib.h>

#include "policybook/cache/cache.h"
#include "policybook/cache/sieve.h"
#include "policybook/cache/traces.h"

int main(void)
{
    /* `zipf-1.0-100k`: 100,000 requests over 10,000 distinct keys drawn from a
     * Zipf distribution — a few keys asked for constantly, a long tail asked
     * for once. That shape is why caching works at all. */
    const pb_cache_trace_spec *trace = pb_cache_trace_find("zipf-1.0-100k");
    pb_cache_sieve_params params = PB_CACHE_SIEVE_PARAMS_DEFAULT;
    pb_cache *cache;
    uint32_t *keys;
    unsigned char *resident; /* the caller's storage; the policy never sees it */
    size_t produced;
    size_t i;
    size_t hits = 0;
    size_t held = 0;

    if (trace == NULL) {
        fprintf(stderr, "no such trace\n");
        return 1;
    }

    keys = (uint32_t *)malloc((size_t)trace->events * sizeof(uint32_t));
    resident = (unsigned char *)calloc((size_t)trace->key_universe, 1u);
    if (keys == NULL || resident == NULL) {
        fprintf(stderr, "out of memory\n");
        free(keys);
        free(resident);
        return 1;
    }

    /* The trace is generated, not recorded: the same 100,000 keys come out of
     * this call in C, Python and TypeScript, which is what makes the hit rate
     * below comparable with the published one. */
    produced = pb_cache_trace_generate(trace, keys, (size_t)trace->events, NULL);

    /* Everything the policy will ever allocate, it allocates here. Nothing on
     * the hot path below calls malloc, which is what makes it usable somewhere
     * an unbounded pause is not acceptable. */
    params.capacity = trace->capacity;
    cache = pb_cache_sieve.create(&params, NULL, NULL);
    if (cache == NULL) {
        fprintf(stderr, "could not create the policy\n");
        free(keys);
        free(resident);
        return 1;
    }

    for (i = 0; i < produced; ++i) {
        uint64_t key = (uint64_t)keys[i];
        bool hit = resident[keys[i]] != 0u;

        /* Tell the policy what happened. The trailing `pb_cache_meta *` is
         * optional — it carries the caller's resident count and clock for the
         * policies that want them, and NULL is fine for the ones that do not. */
        pb_cache_sieve.on_access(cache, key, hit, NULL);

        if (hit) {
            hits += 1;
            continue;
        }

        /* A miss: the caller inserts, then asks the policy who to drop if that
         * put it over capacity. `evict` names a *key*, not a slot — the policy
         * has no idea where the caller keeps anything. */
        resident[keys[i]] = 1u;
        held += 1;

        if (held > (size_t)trace->capacity) {
            uint64_t victim = pb_cache_sieve.evict(cache);
            resident[(uint32_t)victim] = 0u;
            held -= 1;
        }
    }

    printf("SIEVE on %s: %.4f hit rate over %lu requests, %lu bytes of policy state\n",
           trace->id, (double)hits / (double)produced, (unsigned long)produced,
           (unsigned long)pb_cache_sieve.memory_bytes(cache));

    pb_cache_sieve.destroy(cache);
    free(keys);
    free(resident);
    return 0;
}

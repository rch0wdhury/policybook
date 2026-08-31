#include "fuzz_kvcache_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "policybook/kv_cache/h2o.h"
#include "policybook/kv_cache/kv_cache.h"
#include "policybook/kv_cache/pyramidkv.h"
#include "policybook/kv_cache/scissorhands.h"
#include "policybook/kv_cache/snapkv.h"
#include "policybook/kv_cache/sliding_window.h"
#include "policybook/kv_cache/streaming_llm.h"
#include "policybook/kv_cache/tova.h"

/* Reported violations are capped: a broken policy fails on nearly every input,
 * and the first negative test produced hundreds of identical lines that buried
 * the one that mattered (the lesson recorded in T18). */
#define PB_FUZZ_KV_MAX_REPORTS 5

/* --- a counting allocator ---------------------------------------------------
 *
 * The no-allocation-after-create rule is the one invariant `memory_bytes`
 * cannot check, because a policy that freed and reallocated the same number of
 * bytes every step would report a constant size while calling malloc per token.
 * Counting the calls catches that; comparing sizes does not.
 */

typedef struct fuzz_alloc_ctx {
    unsigned long allocs;
    unsigned long frees;
} fuzz_alloc_ctx;

static void *fuzz_alloc(void *ctx, size_t n)
{
    ((fuzz_alloc_ctx *)ctx)->allocs += 1u;
    return malloc(n);
}

static void fuzz_free(void *ctx, void *p, size_t n)
{
    (void)n;
    if (p != NULL) {
        ((fuzz_alloc_ctx *)ctx)->frees += 1u;
    }
    free(p);
}

/* --- the policies -----------------------------------------------------------
 *
 * Each is built from one decoded budget and one secondary parameter, which
 * every policy interprets in its own terms: pinned sinks, a recent window, or
 * nothing at all. They are the same number wearing different labels.
 */

typedef struct fuzz_policy {
    const char *name;
    const pb_kvcache_vtable *vtable;
    pb_kvcache *(*build)(uint32_t budget, uint32_t secondary, const pb_allocator *alloc);
} fuzz_policy;

static pb_kvcache *build_sliding_window(uint32_t budget, uint32_t secondary,
                                        const pb_allocator *alloc)
{
    pb_kvcache_sliding_window_params params;
    (void)secondary;
    params.budget = budget;
    return pb_kvcache_sliding_window.create(&params, alloc, NULL);
}

static pb_kvcache *build_streaming_llm(uint32_t budget, uint32_t secondary,
                                       const pb_allocator *alloc)
{
    pb_kvcache_streaming_llm_params params;
    params.budget = budget;
    params.sinks = secondary;
    return pb_kvcache_streaming_llm.create(&params, alloc, NULL);
}

static pb_kvcache *build_h2o(uint32_t budget, uint32_t secondary, const pb_allocator *alloc)
{
    pb_kvcache_h2o_params params;
    params.budget = budget;
    params.recent_window = secondary;
    return pb_kvcache_h2o.create(&params, alloc, NULL);
}

static pb_kvcache *build_scissorhands(uint32_t budget, uint32_t secondary,
                                      const pb_allocator *alloc)
{
    pb_kvcache_scissorhands_params params;
    params.budget = budget;
    params.recent_window = secondary;
    return pb_kvcache_scissorhands.create(&params, alloc, NULL);
}

static pb_kvcache *build_tova(uint32_t budget, uint32_t secondary, const pb_allocator *alloc)
{
    pb_kvcache_tova_params params;
    (void)secondary;
    params.budget = budget;
    return pb_kvcache_tova.create(&params, alloc, NULL);
}

static pb_kvcache *build_snapkv(uint32_t budget, uint32_t secondary, const pb_allocator *alloc)
{
    pb_kvcache_snapkv_params params;
    params.budget = budget;
    params.recent_window = secondary;
    /* Small and odd: a wide window would make every score equal and a wide
     * kernel would pool the whole cache into one value, and neither exercises
     * the machinery. */
    params.obs_window = 1u + (secondary % 4u);
    params.pool_kernel = 1u + 2u * (secondary % 3u);
    return pb_kvcache_snapkv.create(&params, alloc, NULL);
}

static pb_kvcache *build_pyramidkv(uint32_t budget, uint32_t secondary,
                                   const pb_allocator *alloc)
{
    pb_kvcache_pyramidkv_params params;
    params.budget = budget;
    params.num_layers = 1u + (secondary % 4u);
    params.layer = secondary % params.num_layers;
    params.pyramid_ratio = 1u + (secondary % 5u);
    params.recent_window = secondary;
    params.obs_window = 1u + (secondary % 4u);
    params.pool_kernel = 1u + 2u * (secondary % 3u);
    return pb_kvcache_pyramidkv.create(&params, alloc, NULL);
}

static const fuzz_policy POLICIES[] = {
    { "sliding-window", &pb_kvcache_sliding_window, build_sliding_window },
    { "streaming-llm", &pb_kvcache_streaming_llm, build_streaming_llm },
    { "h2o", &pb_kvcache_h2o, build_h2o },
    { "scissorhands", &pb_kvcache_scissorhands, build_scissorhands },
    { "tova", &pb_kvcache_tova, build_tova },
    { "snapkv", &pb_kvcache_snapkv, build_snapkv },
    { "pyramidkv", &pb_kvcache_pyramidkv, build_pyramidkv }
};

#define POLICY_COUNT (sizeof(POLICIES) / sizeof(POLICIES[0]))

unsigned pb_fuzz_kvcache_policy_count(void)
{
    return (unsigned)POLICY_COUNT;
}

static void report(int *violations, const char *policy, const char *message)
{
    *violations += 1;
    if (*violations <= PB_FUZZ_KV_MAX_REPORTS) {
        fprintf(stderr, "FUZZ %s: %s\n", policy, message);
    }
}

/*
 * Drive one policy through a decoded decode loop.
 *
 * The first three bytes configure the policy; the rest are consumed one per
 * kept position as attention weights, wrapping when they run out. Anything the
 * decoder cannot use is simply not read, so a truncated input is a shorter run
 * rather than an error.
 */
int pb_fuzz_kvcache_once(const uint8_t *data, size_t size)
{
    /* Shadow bookkeeping: what the harness believes the policy holds. Static
     * rather than allocated, so the counting allocator sees only the policy's
     * own calls. */
    static uint32_t kept[PB_FUZZ_KV_MAX_STEPS + 1u];
    static uint8_t is_kept[PB_FUZZ_KV_MAX_STEPS + 1u];
    static uint32_t victims[PB_FUZZ_KV_MAX_BUDGET + 1u];
    static float attn[PB_FUZZ_KV_MAX_BUDGET + 1u];

    fuzz_alloc_ctx counters;
    pb_allocator allocator;
    const fuzz_policy *entry;
    pb_kvcache *policy;
    uint32_t budget;
    uint32_t secondary;
    uint32_t steps;
    uint32_t kept_count;
    uint32_t step;
    size_t cursor;
    size_t span;
    size_t memory_after_create;
    unsigned long allocs_after_create;
    int violations = 0;

    /* Four configuration bytes and at least one weight byte to draw from. The
     * first version tested for four and then wrapped the cursor back to index
     * four without re-checking it, so an input of exactly four bytes read one
     * past the end — which libFuzzer found in twenty-two executions, in the
     * fuzzer rather than in a policy. The harness is code too. */
    if (size < 5u) {
        return 0;
    }

    entry = &POLICIES[data[0] % POLICY_COUNT];
    /* At least two, so there is room for a secondary parameter strictly below
     * it — every scoring policy here refuses a window as wide as the budget. */
    budget = 2u + (uint32_t)(data[1] % (PB_FUZZ_KV_MAX_BUDGET - 1u));
    secondary = 1u + (uint32_t)(data[2] % (budget - 1u));
    steps = 1u + (uint32_t)(data[3] % (PB_FUZZ_KV_MAX_STEPS - 1u));

    counters.allocs = 0u;
    counters.frees = 0u;
    allocator.alloc = fuzz_alloc;
    allocator.free = fuzz_free;
    allocator.ctx = &counters;

    policy = entry->build(budget, secondary, &allocator);
    if (policy == NULL) {
        /* A configuration a policy legitimately refuses. Not a violation. */
        return 0;
    }
    memory_after_create = entry->vtable->memory_bytes(policy);
    allocs_after_create = counters.allocs;

    /* Position 0's token exists before the first decode step, so the cache
     * holds it from the outset — the same seeding the TypeScript harness does. */
    memset(is_kept, 0, sizeof(is_kept));
    kept[0] = 0u;
    is_kept[0] = 1u;
    kept_count = 1u;
    cursor = 0u;
    /* Weight bytes live at [4, size); indexing modulo the span wraps by
     * construction rather than by a guard that has to be got right. */
    span = size - 4u;

    for (step = 1u; step <= steps; ++step) {
        uint32_t i;
        size_t produced;

        /* Attention over the positions the policy still holds, in ascending
         * order, non-negative and never renormalised — as the interface says. */
        for (i = 0u; i < kept_count; ++i) {
            attn[i] = (float)data[4u + (cursor % span)] / 255.0f;
            cursor += 1u;
        }

        entry->vtable->on_decode_step(policy, step, attn, (size_t)kept_count);

        kept[kept_count] = step;
        is_kept[step] = 1u;
        kept_count += 1u;

        if (kept_count > budget) {
            uint32_t write = 0u;
            uint32_t read;

            produced = entry->vtable->evict(policy, budget, victims,
                                            (size_t)(PB_FUZZ_KV_MAX_BUDGET + 1u));

            for (i = 0u; i < (uint32_t)produced; ++i) {
                uint32_t victim = victims[i];
                if (victim > PB_FUZZ_KV_MAX_STEPS) {
                    report(&violations, entry->name, "evicted a position out of range");
                    continue;
                }
                if (is_kept[victim] == 0u) {
                    /* Either never held, or named twice in the same call —
                     * indistinguishable from here, and both are wrong. */
                    report(&violations, entry->name,
                           "evicted a position it does not hold, or named one twice");
                    continue;
                }
                is_kept[victim] = 0u;
            }

            /* Rebuild the shadow kept list from what survived. */
            for (read = 0u; read < kept_count; ++read) {
                if (is_kept[kept[read]] != 0u) {
                    kept[write] = kept[read];
                    write += 1u;
                }
            }
            kept_count = write;

            if (kept_count > budget) {
                report(&violations, entry->name,
                       "evict did not free enough to reach the budget");
                /* Nothing sensible follows from an over-budget cache, and
                 * continuing would report the same failure every step. */
                break;
            }
        }

        if (counters.allocs != allocs_after_create) {
            report(&violations, entry->name, "allocated after create");
            break;
        }
        if (entry->vtable->memory_bytes(policy) != memory_after_create) {
            report(&violations, entry->name, "memory_bytes changed after create");
            break;
        }
    }

    entry->vtable->destroy(policy);

    /* Everything taken is given back. A leak would be ASan's business under the
     * sanitizer builds, but the standalone driver runs without it. */
    if (counters.frees != counters.allocs) {
        report(&violations, entry->name, "destroy did not release every allocation");
    }

    return violations;
}

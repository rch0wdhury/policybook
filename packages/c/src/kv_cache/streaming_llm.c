/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/streaming-llm/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * StreamingLLM — a sliding window that also pins the first few tokens.
 *
 * Mirrors index.ts and policy.py. The sliding window's ring buffer with the
 * sinks held outside it: a position below `sinks` is pinned and never enters
 * the ring, so tracking them costs a counter rather than storage.
 */

#include "policybook/kv_cache/streaming_llm.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"

typedef struct pb_kvcache_streaming_llm_state {
    const pb_allocator *allocator;
    uint32_t sinks;      /* positions below this are pinned */
    uint32_t sinks_held; /* how many of them have been seen */
    uint32_t capacity;   /* budget - sinks + 1 */
    uint32_t *slots;     /* the recency window, oldest at head */
    uint32_t head;
    uint32_t size;
} pb_kvcache_streaming_llm_state;

static pb_kvcache *streaming_llm_create(const void *params, const pb_allocator *allocator,
                                        pb_rng *rng)
{
    const pb_kvcache_streaming_llm_params *config =
        (const pb_kvcache_streaming_llm_params *)params;
    pb_kvcache_streaming_llm_state *self;
    uint32_t budget;
    uint32_t sinks;
    uint32_t capacity;

    (void)rng; /* entirely deterministic */

    budget = (config == NULL) ? 512u : config->budget;
    sinks = (config == NULL) ? 4u : config->sinks;
    /* With zero sinks the ring holds budget + 1 slots; a budget of UINT32_MAX
     * would wrap it. */
    if (budget == 0u || budget == UINT32_MAX) {
        return NULL;
    }
    /* With no room left over the policy could not keep the newest token, which
     * would behave nothing like the paper. */
    if (sinks >= budget) {
        return NULL;
    }
    capacity = budget - sinks + 1u;

    self = (pb_kvcache_streaming_llm_state *)pb_alloc(
        allocator, sizeof(pb_kvcache_streaming_llm_state));
    if (self == NULL) {
        return NULL;
    }

    self->slots = (uint32_t *)pb_alloc(allocator, (size_t)capacity * sizeof(uint32_t));
    if (self->slots == NULL) {
        pb_free(allocator, self, sizeof(pb_kvcache_streaming_llm_state));
        return NULL;
    }

    self->allocator = allocator;
    self->sinks = sinks;
    self->capacity = capacity;
    self->head = 0u;
    self->size = 0u;
    self->sinks_held = 0u;

    /* Position 0's token exists before the first decode step (see kv_cache.h).
     * It is a sink when any are configured, and otherwise the first window
     * entry. */
    if (sinks > 0u) {
        self->sinks_held = 1u;
    } else {
        self->slots[0] = 0u;
        self->size = 1u;
    }
    return (pb_kvcache *)self;
}

static void streaming_llm_destroy(pb_kvcache *policy)
{
    pb_kvcache_streaming_llm_state *self = (pb_kvcache_streaming_llm_state *)policy;

    if (self == NULL) {
        return;
    }
    pb_free(self->allocator, self->slots, (size_t)self->capacity * sizeof(uint32_t));
    pb_free(self->allocator, self, sizeof(pb_kvcache_streaming_llm_state));
}

static void streaming_llm_on_decode_step(pb_kvcache *policy, uint32_t pos, const float *attn,
                                         size_t attn_len)
{
    pb_kvcache_streaming_llm_state *self = (pb_kvcache_streaming_llm_state *)policy;
    uint32_t slot;

    assert(self != NULL);

    /* This policy pins the structurally special positions, not the important
     * ones, so it never looks at a weight. */
    (void)attn;
    (void)attn_len;

    if (pos < self->sinks) {
        self->sinks_held += 1u;
        return;
    }

    /* Holding more than the window capacity means the caller's budget does not
     * match the policy's, which is a configuration error. */
    assert(self->size < self->capacity);
    if (self->size >= self->capacity) {
        return;
    }

    slot = self->head + self->size;
    if (slot >= self->capacity) {
        slot -= self->capacity;
    }
    self->slots[slot] = pos;
    self->size += 1u;
}

static size_t streaming_llm_evict(pb_kvcache *policy, uint32_t budget, uint32_t *victims,
                                  size_t capacity)
{
    pb_kvcache_streaming_llm_state *self = (pb_kvcache_streaming_llm_state *)policy;
    uint32_t held;
    uint32_t needed;
    size_t written = 0;

    assert(self != NULL);
    assert(victims != NULL);

    held = self->sinks_held + self->size;
    if (held <= budget) {
        return 0;
    }

    /* The sinks are never evicted, so at most the whole window can go — which
     * is why a budget below the sink count cannot be met and is not pretended
     * to be. */
    needed = held - budget;
    if (needed > self->size) {
        needed = self->size;
    }
    if ((size_t)needed > capacity) {
        return 0;
    }

    while (written < (size_t)needed) {
        victims[written] = self->slots[self->head];
        written += 1;
        self->head += 1u;
        if (self->head == self->capacity) {
            self->head = 0u;
        }
        self->size -= 1u;
    }
    return written;
}

static size_t streaming_llm_memory_bytes(const pb_kvcache *policy)
{
    const pb_kvcache_streaming_llm_state *self =
        (const pb_kvcache_streaming_llm_state *)policy;

    if (self == NULL) {
        return sizeof(pb_kvcache_streaming_llm_state);
    }
    return sizeof(pb_kvcache_streaming_llm_state) +
           (size_t)self->capacity * sizeof(uint32_t);
}

const pb_kvcache_vtable pb_kvcache_streaming_llm = { streaming_llm_create,
                                                     streaming_llm_on_decode_step,
                                                     streaming_llm_evict,
                                                     streaming_llm_memory_bytes,
                                                     streaming_llm_destroy };

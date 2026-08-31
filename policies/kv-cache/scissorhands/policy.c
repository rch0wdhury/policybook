/*
 * Scissorhands — count how many steps a token mattered for, not how much.
 *
 * Mirrors index.ts and policy.py. Structurally h2o with a vote counter in place
 * of a cumulative score, which is the whole of the difference between the two
 * policies and the reason they diverge on a stale early spike.
 */

#include "policybook/kv_cache/scissorhands.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"

typedef struct pb_kvcache_scissorhands_state {
    const pb_allocator *allocator;
    uint32_t recent_window;
    uint32_t capacity; /* budget + 1 */
    uint32_t size;
    uint32_t *positions; /* kept positions, ascending */
    uint32_t *votes;     /* steps beating the fair share, parallel to positions */
    uint8_t *doomed;     /* eviction scratch, so evict allocates nothing */
} pb_kvcache_scissorhands_state;

static void scissorhands_release(pb_kvcache_scissorhands_state *self)
{
    size_t slots;

    if (self == NULL) {
        return;
    }
    slots = (size_t)self->capacity;
    pb_free(self->allocator, self->positions, slots * sizeof(uint32_t));
    pb_free(self->allocator, self->votes, slots * sizeof(uint32_t));
    pb_free(self->allocator, self->doomed, slots * sizeof(uint8_t));
    pb_free(self->allocator, self, sizeof(pb_kvcache_scissorhands_state));
}

static pb_kvcache *scissorhands_create(const void *params, const pb_allocator *allocator,
                                       pb_rng *rng)
{
    const pb_kvcache_scissorhands_params *config =
        (const pb_kvcache_scissorhands_params *)params;
    pb_kvcache_scissorhands_state *self;
    uint32_t budget;
    uint32_t recent_window;
    size_t slots;

    (void)rng; /* entirely deterministic */

    budget = (config == NULL) ? 512u : config->budget;
    recent_window = (config == NULL) ? 32u : config->recent_window;
    /* The slot arrays hold budget + 1 entries; UINT32_MAX would wrap that. */
    if (budget == 0u || budget == UINT32_MAX) {
        return NULL;
    }
    /* With the whole budget protected there would be nothing left to evict. */
    if (recent_window >= budget) {
        return NULL;
    }

    self = (pb_kvcache_scissorhands_state *)pb_alloc(
        allocator, sizeof(pb_kvcache_scissorhands_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->recent_window = recent_window;
    self->capacity = budget + 1u;
    self->size = 0u;
    slots = (size_t)self->capacity;

    self->positions = (uint32_t *)pb_alloc(allocator, slots * sizeof(uint32_t));
    self->votes = (uint32_t *)pb_alloc(allocator, slots * sizeof(uint32_t));
    self->doomed = (uint8_t *)pb_alloc(allocator, slots * sizeof(uint8_t));
    if (self->positions == NULL || self->votes == NULL || self->doomed == NULL) {
        scissorhands_release(self);
        return NULL;
    }

    /* Position 0's token exists before the first decode step (see kv_cache.h),
     * and has voted on nothing yet. */
    self->positions[0] = 0u;
    self->votes[0] = 0u;
    self->doomed[0] = 0u;
    self->size = 1u;
    return (pb_kvcache *)self;
}

static void scissorhands_destroy(pb_kvcache *policy)
{
    scissorhands_release((pb_kvcache_scissorhands_state *)policy);
}

static void scissorhands_on_decode_step(pb_kvcache *policy, uint32_t pos, const float *attn,
                                        size_t attn_len)
{
    pb_kvcache_scissorhands_state *self = (pb_kvcache_scissorhands_state *)policy;
    double share;
    size_t shared;
    size_t i;

    assert(self != NULL);

    if (attn != NULL && attn_len > 0u) {
        /* One double division per step, not per position. */
        share = 1.0 / (double)attn_len;
        shared = (attn_len < (size_t)self->size) ? attn_len : (size_t)self->size;
        for (i = 0; i < shared; ++i) {
            /* Strictly greater: a position that exactly matches its share does
             * not vote, which a vector pins. */
            if ((double)attn[i] > share) {
                self->votes[i] += 1u;
            }
        }
    }

    /* Holding more than budget + 1 means the caller's budget does not match the
     * policy's, which is a configuration error rather than a decision to make. */
    assert(self->size < self->capacity);
    if (self->size >= self->capacity) {
        return;
    }

    self->positions[self->size] = pos;
    self->votes[self->size] = 0u;
    self->doomed[self->size] = 0u;
    self->size += 1u;
}

static size_t scissorhands_evict(pb_kvcache *policy, uint32_t budget, uint32_t *victims,
                                 size_t capacity)
{
    pb_kvcache_scissorhands_state *self = (pb_kvcache_scissorhands_state *)policy;
    uint32_t protected_count;
    uint32_t evictable_end;
    uint32_t needed;
    uint32_t taken;
    uint32_t read;
    uint32_t write = 0u;
    size_t written = 0;

    assert(self != NULL);
    assert(victims != NULL);

    if (self->size <= budget) {
        return 0;
    }
    needed = self->size - budget;

    protected_count = (self->recent_window < self->size) ? self->recent_window : self->size;
    evictable_end = self->size - protected_count;
    if (needed > evictable_end) {
        needed = evictable_end;
    }

    /* Refusing outright rather than evicting as much as fits: a partial
     * eviction would leave the caller over budget with no way to tell that the
     * buffer, not the policy, was the limit. */
    if ((size_t)needed > capacity) {
        return 0;
    }

    for (taken = 0u; taken < needed; ++taken) {
        uint32_t best = evictable_end; /* sentinel: nothing chosen yet */
        uint32_t i;
        for (i = 0u; i < evictable_end; ++i) {
            if (self->doomed[i] != 0u) {
                continue;
            }
            /* Strictly less, so a tie leaves the earlier index standing — the
             * lower position, since the array is ascending. */
            if (best == evictable_end || self->votes[i] < self->votes[best]) {
                best = i;
            }
        }
        if (best == evictable_end) {
            break;
        }
        self->doomed[best] = 1u;
    }

    /* One compacting pass: victims come out in ascending position order. */
    for (read = 0u; read < self->size; ++read) {
        if (self->doomed[read] != 0u) {
            self->doomed[read] = 0u;
            victims[written] = self->positions[read];
            written += 1;
            continue;
        }
        self->positions[write] = self->positions[read];
        self->votes[write] = self->votes[read];
        write += 1u;
    }
    self->size = write;
    return written;
}

static size_t scissorhands_memory_bytes(const pb_kvcache *policy)
{
    const pb_kvcache_scissorhands_state *self = (const pb_kvcache_scissorhands_state *)policy;
    size_t slots;

    if (self == NULL) {
        return sizeof(pb_kvcache_scissorhands_state);
    }
    slots = (size_t)self->capacity;
    return sizeof(pb_kvcache_scissorhands_state) + slots * sizeof(uint32_t) +
           slots * sizeof(uint32_t) + slots * sizeof(uint8_t);
}

const pb_kvcache_vtable pb_kvcache_scissorhands = { scissorhands_create,
                                                    scissorhands_on_decode_step,
                                                    scissorhands_evict,
                                                    scissorhands_memory_bytes,
                                                    scissorhands_destroy };

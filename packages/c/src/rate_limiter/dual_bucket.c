/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/dual-bucket/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * DualBucket — two limits at once, and a request must satisfy both.
 *
 * Mirrors index.ts and policy.py. A map from key to slot, and five parallel
 * arrays: two balances, two carries, and when the slot was last brought up to
 * date.
 */

#include "policybook/rate_limiter/dual_bucket.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"
#include "policybook/ds/map.h"

/* Both ceilings are stated per minute, so the ledger's period is a minute. */
#define PB_DUAL_PERIOD_MS 60000u

typedef struct pb_ratelimiter_dual_bucket_state {
    const pb_allocator *allocator;
    pb_map index; /* key -> slot */
    uint32_t *requests;
    uint32_t *request_credit;
    uint32_t *tokens;
    uint32_t *token_credit;
    uint64_t *last;
    uint32_t requests_per_min;
    uint32_t tokens_per_min;
    uint32_t max_keys;
    uint32_t used;
} pb_ratelimiter_dual_bucket_state;

/*
 * Advance one dimension's ledger.
 *
 * The token bucket's integer ledger with a period of a minute instead of a
 * second: the fraction lives in a credit measured in PB_DUAL_PERIOD_MS-ths of a
 * permit, so nothing is ever rounded away.
 */
static void dual_bucket_advance(uint32_t *balance, uint32_t *credit, uint32_t rate_per_min,
                    uint64_t elapsed)
{
    uint64_t accrued = (uint64_t)*credit + (uint64_t)rate_per_min * elapsed;
    uint64_t whole = (uint64_t)*balance + accrued / PB_DUAL_PERIOD_MS;

    accrued %= PB_DUAL_PERIOD_MS;
    if (whole >= (uint64_t)rate_per_min) {
        whole = (uint64_t)rate_per_min;
        accrued = 0u;
    }

    *balance = (uint32_t)whole;
    *credit = (uint32_t)accrued;
}

/*
 * Bring both ledgers up to date at `now_ms`.
 *
 * Elapsed time is clamped to one period, which is exactly how long a drained
 * bucket takes to dual_bucket_refill — beyond that the result cannot change, and clamping
 * keeps the multiply well inside 64 bits.
 */
static void dual_bucket_refill(pb_ratelimiter_dual_bucket_state *self, uint32_t slot, uint64_t now_ms)
{
    uint64_t elapsed;

    if (now_ms <= self->last[slot]) {
        return;
    }
    elapsed = now_ms - self->last[slot];
    if (elapsed > (uint64_t)PB_DUAL_PERIOD_MS) {
        elapsed = (uint64_t)PB_DUAL_PERIOD_MS;
    }

    dual_bucket_advance(&self->requests[slot], &self->request_credit[slot], self->requests_per_min, elapsed);
    dual_bucket_advance(&self->tokens[slot], &self->token_credit[slot], self->tokens_per_min, elapsed);
    self->last[slot] = now_ms;
}

/*
 * Find a key's slot, claiming a free one if it has never been seen.
 *
 * Returns false when the table is full, which is the fail-closed case: a new
 * key is refused rather than being silently let through.
 */
static bool dual_bucket_slot_for(pb_ratelimiter_dual_bucket_state *self, uint64_t key, uint64_t now_ms,
                     uint32_t *slot)
{
    if (pb_map_get(&self->index, key, slot)) {
        dual_bucket_refill(self, *slot, now_ms);
        return true;
    }
    if (self->used >= self->max_keys) {
        return false;
    }
    *slot = self->used;
    if (!pb_map_put(&self->index, key, *slot)) {
        return false;
    }
    self->used += 1u;
    self->requests[*slot] = self->requests_per_min;
    self->request_credit[*slot] = 0u;
    self->tokens[*slot] = self->tokens_per_min;
    self->token_credit[*slot] = 0u;
    self->last[*slot] = now_ms;
    return true;
}

static pb_ratelimiter *dual_bucket_create(const void *params, const pb_allocator *allocator,
                                          pb_rng *rng)
{
    const pb_ratelimiter_dual_bucket_params *config =
        (const pb_ratelimiter_dual_bucket_params *)params;
    pb_ratelimiter_dual_bucket_state *self;
    uint32_t requests_per_min;
    uint32_t tokens_per_min;
    uint32_t max_keys;
    bool ok;

    (void)rng; /* a dual bucket makes no random choices */

    requests_per_min = (config == NULL) ? 500u : config->requests_per_min;
    tokens_per_min = (config == NULL) ? 200000u : config->tokens_per_min;
    max_keys = (config == NULL) ? 1024u : config->max_keys;
    if (requests_per_min == 0u || tokens_per_min == 0u || max_keys == 0u) {
        return NULL;
    }

    self = (pb_ratelimiter_dual_bucket_state *)pb_alloc(
        allocator, sizeof(pb_ratelimiter_dual_bucket_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->requests_per_min = requests_per_min;
    self->tokens_per_min = tokens_per_min;
    self->max_keys = max_keys;
    self->used = 0u;
    self->requests = NULL;
    self->request_credit = NULL;
    self->tokens = NULL;
    self->token_credit = NULL;
    self->last = NULL;

    if (!pb_map_init(&self->index, max_keys, allocator)) {
        pb_free(allocator, self, sizeof(pb_ratelimiter_dual_bucket_state));
        return NULL;
    }

    self->requests = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->request_credit = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->tokens = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->token_credit = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->last = (uint64_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint64_t));

    ok = self->requests != NULL && self->request_credit != NULL && self->tokens != NULL &&
         self->token_credit != NULL && self->last != NULL;
    if (!ok) {
        pb_free(allocator, self->requests, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->request_credit, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->tokens, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->token_credit, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->last, (size_t)max_keys * sizeof(uint64_t));
        pb_map_destroy(&self->index, allocator);
        pb_free(allocator, self, sizeof(pb_ratelimiter_dual_bucket_state));
        return NULL;
    }

    return (pb_ratelimiter *)self;
}

static void dual_bucket_destroy(pb_ratelimiter *limiter)
{
    pb_ratelimiter_dual_bucket_state *self = (pb_ratelimiter_dual_bucket_state *)limiter;
    const pb_allocator *allocator;
    size_t words;
    size_t stamps;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    words = (size_t)self->max_keys * sizeof(uint32_t);
    stamps = (size_t)self->max_keys * sizeof(uint64_t);

    pb_free(allocator, self->requests, words);
    pb_free(allocator, self->request_credit, words);
    pb_free(allocator, self->tokens, words);
    pb_free(allocator, self->token_credit, words);
    pb_free(allocator, self->last, stamps);
    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self, sizeof(pb_ratelimiter_dual_bucket_state));
}

static bool dual_bucket_allow(pb_ratelimiter *limiter, uint64_t key, uint32_t cost,
                              uint64_t now_ms)
{
    pb_ratelimiter_dual_bucket_state *self = (pb_ratelimiter_dual_bucket_state *)limiter;
    uint32_t slot;

    assert(self != NULL);

    if (!dual_bucket_slot_for(self, key, now_ms, &slot)) {
        return false;
    }

    /* Both dimensions are tested before either is charged. A caller refused for
     * work must not have quietly spent a request too, or retrying would
     * throttle it harder than not retrying. */
    if (self->requests[slot] < 1u) {
        return false;
    }
    if (self->tokens[slot] < cost) {
        return false;
    }

    self->requests[slot] -= 1u;
    self->tokens[slot] -= cost;
    return true;
}

/* Milliseconds until one whole permit accrues on a dimension. */
static uint64_t wait_for(uint32_t available, uint32_t credit, uint32_t rate_per_min)
{
    uint64_t deficit;

    if (available >= 1u) {
        return 0u;
    }
    deficit = (uint64_t)PB_DUAL_PERIOD_MS - (uint64_t)credit;
    return (deficit + (uint64_t)rate_per_min - 1u) / (uint64_t)rate_per_min;
}

static uint64_t dual_bucket_retry_after(pb_ratelimiter *limiter, uint64_t key, uint64_t now_ms)
{
    pb_ratelimiter_dual_bucket_state *self = (pb_ratelimiter_dual_bucket_state *)limiter;
    uint64_t by_requests;
    uint64_t by_tokens;
    uint32_t slot;

    assert(self != NULL);

    if (!pb_map_get(&self->index, key, &slot)) {
        /* Untracked. With room in the table this key would be admitted right
         * now, so zero is the truth; with the table full it will never be
         * admitted, and zero would be a lie a caller acts on. */
        return self->used >= self->max_keys ? PB_RATELIMITER_RETRY_UNKNOWN : 0u;
    }
    dual_bucket_refill(self, slot, now_ms);

    by_requests =
        wait_for(self->requests[slot], self->request_credit[slot], self->requests_per_min);
    by_tokens = wait_for(self->tokens[slot], self->token_credit[slot], self->tokens_per_min);
    return by_requests > by_tokens ? by_requests : by_tokens;
}

static size_t dual_bucket_state_size(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_dual_bucket_state *self =
        (const pb_ratelimiter_dual_bucket_state *)limiter;
    assert(self != NULL);
    return (size_t)self->used;
}

static size_t dual_bucket_memory_bytes(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_dual_bucket_state *self =
        (const pb_ratelimiter_dual_bucket_state *)limiter;
    assert(self != NULL);
    return sizeof(pb_ratelimiter_dual_bucket_state) +
           (size_t)self->max_keys * (4u * sizeof(uint32_t) + sizeof(uint64_t)) +
           pb_map_memory_bytes(&self->index);
}

const pb_ratelimiter_vtable pb_ratelimiter_dual_bucket = { dual_bucket_create,
                                                           dual_bucket_allow,
                                                           dual_bucket_retry_after,
                                                           dual_bucket_state_size,
                                                           dual_bucket_memory_bytes,
                                                           dual_bucket_destroy };

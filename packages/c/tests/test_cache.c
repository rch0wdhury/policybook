/*
 * What the shared vectors cannot check about the C caches: create-time bounds.
 *
 * TypeScript sizes its tables in float64 and accepts any capacity; C sizes
 * them in fixed-width integers, so a capacity whose tables cannot be addressed
 * has to be refused in create (concept.md §12.2). The refusal, and the sizing
 * arithmetic just inside it, are what this file pins — through a probing
 * allocator, because the boundary tables run to gigabytes no CI machine
 * should actually allocate.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "policybook/allocator.h"
#include "policybook/cache/w_tinylfu.h"

#include "pb_test.h"

/*
 * Records every requested size and refuses anything above `cap_bytes`, so a
 * create can be walked up to its boundary without gigabytes changing hands.
 */
#define PROBE_SLOTS 16

typedef struct probing_allocator {
    size_t requested[PROBE_SLOTS];
    int requests;
    size_t live_bytes;
    size_t cap_bytes;
} probing_allocator;

static void *probing_alloc(void *ctx, size_t n)
{
    probing_allocator *state = (probing_allocator *)ctx;

    if (state->requests < PROBE_SLOTS) {
        state->requested[state->requests] = n;
    }
    state->requests += 1;
    if (n > state->cap_bytes) {
        return NULL;
    }
    state->live_bytes += n;
    return pb_allocator_default()->alloc(NULL, n);
}

static void probing_free(void *ctx, void *p, size_t n)
{
    probing_allocator *state = (probing_allocator *)ctx;

    if (p == NULL) {
        return;
    }
    state->live_bytes -= n;
    pb_allocator_default()->free(NULL, p, n);
}

static void probing_reset(probing_allocator *state, size_t cap_bytes)
{
    int i;

    for (i = 0; i < PROBE_SLOTS; ++i) {
        state->requested[i] = 0;
    }
    state->requests = 0;
    state->live_bytes = 0;
    state->cap_bytes = cap_bytes;
}

/*
 * W-TinyLFU's bound is capacity 2^27.
 *
 * The sketch is four rows of next_pow2(8 * capacity) four-bit counters,
 * addressed as row * width + slot in 32 bits, so 4 * width must stay within
 * 2^32. At the bound the sketch is 2 * 2^30 = 2^31 bytes — a size the old
 * uint32 arithmetic (4 * width / 2) wrapped to zero, which pb_alloc granted
 * and the first record() then wrote two gigabytes past.
 */
static void test_w_tinylfu_capacity_bound(void)
{
    pb_cache_w_tinylfu_params params = PB_CACHE_W_TINYLFU_PARAMS_DEFAULT;
    probing_allocator state;
    pb_allocator allocator = { probing_alloc, probing_free, &state };
    pb_cache *cache;
    bool saw_sketch = false;
    int i;

    /* Just above the bound: refused before the allocator is ever consulted. */
    probing_reset(&state, (size_t)4 * 1024 * 1024);
    params.capacity = (1u << 27) + 1u;
    cache = pb_cache_w_tinylfu.create(&params, &allocator, NULL);
    PB_CHECK(cache == NULL);
    PB_CHECK(state.requests == 0);

    /* At the bound: past the guard, and every table is asked for at its true
     * size — the sketch at 2^31 bytes, never zero. The probe refuses the
     * gigabytes, and create hands back everything it was granted. */
    params.capacity = 1u << 27;
    cache = pb_cache_w_tinylfu.create(&params, &allocator, NULL);
    PB_CHECK(cache == NULL);
    PB_CHECK(state.requests >= 8);
    for (i = 0; i < state.requests && i < PROBE_SLOTS; ++i) {
        PB_CHECK(state.requested[i] != 0u);
        if (state.requested[i] == ((size_t)1 << 31)) {
            saw_sketch = true;
        }
    }
    PB_CHECK(saw_sketch);
    PB_CHECK_U64(state.live_bytes, 0u);

    /* Well inside the bound the same allocator serves a real cache. */
    probing_reset(&state, (size_t)4 * 1024 * 1024);
    params.capacity = 4096u;
    cache = pb_cache_w_tinylfu.create(&params, &allocator, NULL);
    PB_CHECK(cache != NULL);
    if (cache != NULL) {
        pb_cache_w_tinylfu.on_access(cache, 1u, false, NULL);
        pb_cache_w_tinylfu.on_access(cache, 1u, true, NULL);
        pb_cache_w_tinylfu.destroy(cache);
    }
    PB_CHECK_U64(state.live_bytes, 0u);
}

int main(void)
{
    test_w_tinylfu_capacity_bound();
    return pb_test_summary("cache C behaviour");
}

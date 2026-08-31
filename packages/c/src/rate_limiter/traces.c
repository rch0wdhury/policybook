#include "policybook/rate_limiter/traces.h"

#include <string.h>

#include "policybook/rng.h"
#include "policybook/zipf.h"

/* The `bursty` cycle: this long in total, of which the first part is ON. */
#define PB_BURST_CYCLE_MS 2000u
#define PB_BURST_ON_MS 200u

const pb_ratelimiter_trace_spec pb_ratelimiter_traces[] = {
    { "steady", PB_RATELIMITER_TRACE_SINGLE_KEY, 60000u, 1u, 0u, 0.09, 50u },
    { "bursty", PB_RATELIMITER_TRACE_BURSTY, 60000u, 1u, 0u, 0.5, 51u },
    { "many-keys", PB_RATELIMITER_TRACE_MANY_KEYS, 120000u, 10000u, 10000u, 0.5, 52u },
    /* Three times the reference limit, sustained. The same generator as
     * `steady` at a rate no limiter can meet. */
    { "overload", PB_RATELIMITER_TRACE_SINGLE_KEY, 60000u, 1u, 0u, 0.3, 53u }
};

const pb_ratelimiter_trace_spec *pb_ratelimiter_trace_find(const char *id)
{
    size_t i;

    if (id == NULL) {
        return NULL;
    }
    for (i = 0; i < (size_t)PB_RATELIMITER_TRACE_COUNT; ++i) {
        if (strcmp(pb_ratelimiter_traces[i].id, id) == 0) {
            return &pb_ratelimiter_traces[i];
        }
    }
    return NULL;
}

/*
 * A steady stream on a single key: one Bernoulli draw per millisecond.
 *
 * Shared by `steady` and `overload`, which differ only in the arrival rate —
 * one just under the reference limit and one three times over it.
 */
static size_t generate_single_key(const pb_ratelimiter_trace_spec *spec, uint32_t *times,
                                  uint32_t *keys, size_t limit)
{
    pb_rng rng;
    size_t written = 0;
    uint32_t t;

    pb_rng_init(&rng, spec->seed);

    for (t = 0; t < spec->duration_ms && written < limit; ++t) {
        if (pb_rng_next_float(&rng) < spec->arrival_p) {
            times[written] = t;
            keys[written] = 0u;
            written += 1;
        }
    }
    return written;
}

/*
 * Silence, then a burst, repeating.
 *
 * A millisecond in the OFF phase consumes no random draw. That is the pinned
 * call order and every port matches it: drawing during the silence would be
 * equally valid as a definition and would produce a completely different trace
 * from the same seed.
 */
static size_t generate_bursty(const pb_ratelimiter_trace_spec *spec, uint32_t *times,
                              uint32_t *keys, size_t limit)
{
    pb_rng rng;
    size_t written = 0;
    uint32_t t;

    pb_rng_init(&rng, spec->seed);

    for (t = 0; t < spec->duration_ms && written < limit; ++t) {
        if (t % PB_BURST_CYCLE_MS >= PB_BURST_ON_MS) {
            continue;
        }
        if (pb_rng_next_float(&rng) < spec->arrival_p) {
            times[written] = t;
            keys[written] = 0u;
            written += 1;
        }
    }
    return written;
}

/*
 * A busy stream spread over a skewed keyspace.
 *
 * The draw order is pinned: the Bernoulli draw comes first, and the Zipf sample
 * is taken only when that draw produced an arrival. Sampling a key
 * unconditionally would consume the stream at a different rate and diverge.
 */
static size_t generate_many_keys(const pb_ratelimiter_trace_spec *spec, uint32_t *times,
                                 uint32_t *keys, size_t limit,
                                 const pb_allocator *allocator)
{
    pb_rng rng;
    pb_zipf zipf;
    size_t written = 0;
    uint32_t t;

    pb_rng_init(&rng, spec->seed);
    if (!pb_zipf_init(&zipf, spec->keyspace, 1.0, allocator)) {
        return 0;
    }

    for (t = 0; t < spec->duration_ms && written < limit; ++t) {
        if (pb_rng_next_float(&rng) < spec->arrival_p) {
            times[written] = t;
            keys[written] = pb_zipf_sample(&zipf, &rng);
            written += 1;
        }
    }

    pb_zipf_destroy(&zipf, allocator);
    return written;
}

size_t pb_ratelimiter_trace_generate(const pb_ratelimiter_trace_spec *spec, uint32_t *times,
                                     uint32_t *keys, size_t max_events,
                                     const pb_allocator *allocator)
{
    if (spec == NULL || times == NULL || keys == NULL || max_events == 0) {
        return 0;
    }

    switch (spec->kind) {
    case PB_RATELIMITER_TRACE_SINGLE_KEY:
        return generate_single_key(spec, times, keys, max_events);
    case PB_RATELIMITER_TRACE_BURSTY:
        return generate_bursty(spec, times, keys, max_events);
    case PB_RATELIMITER_TRACE_MANY_KEYS:
        return generate_many_keys(spec, times, keys, max_events, allocator);
    default:
        return 0;
    }
}

#include "policybook/cache/traces.h"

#include <assert.h>
#include <string.h>

#include "policybook/rng.h"
#include "policybook/zipf.h"

/* How often the scan-heavy trace injects a scan. */
#define PB_SCAN_INTERVAL 20000u
/* How far the popular set rotates each time, in keys. */
#define PB_POPULARITY_SHIFT 2500u
/* How often the popular set rotates. */
#define PB_SHIFT_INTERVAL 25000u

const pb_cache_trace_spec pb_cache_traces[] = {
    { "zipf-1.0-100k", PB_CACHE_TRACE_ZIPF, 1.0, 10000u, 10000u, 1000u, 100000u, 42u },
    { "zipf-0.75-1m", PB_CACHE_TRACE_ZIPF, 0.75, 100000u, 100000u, 10000u, 1000000u, 43u },
    /* The scans emit keys above the Zipf keyspace: 10,000 .. 17,999. */
    { "scan-heavy", PB_CACHE_TRACE_SCAN_HEAVY, 1.0, 10000u, 18000u, 1000u, 108000u, 44u },
    { "shifting-popularity", PB_CACHE_TRACE_SHIFTING, 1.0, 10000u, 10000u, 1000u, 100000u, 45u }
};

const pb_cache_trace_spec *pb_cache_trace_find(const char *id)
{
    size_t i;

    if (id == NULL) {
        return NULL;
    }
    for (i = 0; i < (size_t)PB_CACHE_TRACE_COUNT; ++i) {
        if (strcmp(pb_cache_traces[i].id, id) == 0) {
            return &pb_cache_traces[i];
        }
    }
    return NULL;
}

/* Plain Zipf: draw a rank, emit it as the key. */
static size_t generate_zipf(const pb_cache_trace_spec *spec, uint32_t *out, size_t limit,
                            const pb_allocator *allocator)
{
    pb_rng rng;
    pb_zipf zipf;
    size_t index;

    pb_rng_init(&rng, spec->seed);
    if (!pb_zipf_init(&zipf, spec->keyspace, spec->alpha, allocator)) {
        return 0;
    }

    for (index = 0; index < limit; ++index) {
        out[index] = pb_zipf_sample(&zipf, &rng);
    }

    pb_zipf_destroy(&zipf, allocator);
    return limit;
}

/*
 * Zipf with periodic sequential scans.
 *
 * Every PB_SCAN_INTERVAL accesses, a run of 2 x capacity fresh keys sweeps
 * through — the shape of a backup, a table scan, or a crawler. LRU caches all of
 * it and throws away its working set; a scan-resistant policy barely notices.
 * Scan keys live above the Zipf keyspace so they can never collide with it.
 */
static size_t generate_scan_heavy(const pb_cache_trace_spec *spec, uint32_t *out, size_t limit,
                                  const pb_allocator *allocator)
{
    pb_rng rng;
    pb_zipf zipf;
    size_t written = 0;
    uint32_t scan_index = 0;
    uint32_t scan_length = spec->capacity * 2u;
    uint32_t step;

    pb_rng_init(&rng, spec->seed);
    if (!pb_zipf_init(&zipf, spec->keyspace, spec->alpha, allocator)) {
        return 0;
    }

    for (step = 0; step < spec->events && written < limit; ++step) {
        if (step > 0u && step % PB_SCAN_INTERVAL == 0u) {
            uint32_t scan_base = spec->keyspace + scan_index * scan_length;
            uint32_t offset;
            for (offset = 0; offset < scan_length && written < limit; ++offset) {
                out[written] = scan_base + offset;
                written += 1u;
            }
            scan_index += 1u;
            if (written >= limit) {
                break;
            }
        }

        out[written] = pb_zipf_sample(&zipf, &rng);
        written += 1u;
    }

    pb_zipf_destroy(&zipf, allocator);
    return written;
}

/*
 * Zipf whose popular set rotates.
 *
 * The rank drawn is the same as ever, but it is offset by a rotation that
 * advances every PB_SHIFT_INTERVAL accesses, so yesterday's hot keys go cold.
 * Frequency-based policies that never forget keep the wrong entries.
 */
static size_t generate_shifting(const pb_cache_trace_spec *spec, uint32_t *out, size_t limit,
                                const pb_allocator *allocator)
{
    pb_rng rng;
    pb_zipf zipf;
    size_t step;

    pb_rng_init(&rng, spec->seed);
    if (!pb_zipf_init(&zipf, spec->keyspace, spec->alpha, allocator)) {
        return 0;
    }

    for (step = 0; step < limit; ++step) {
        uint32_t rank = pb_zipf_sample(&zipf, &rng);
        uint32_t shift = (uint32_t)(step / PB_SHIFT_INTERVAL) * PB_POPULARITY_SHIFT;
        out[step] = (rank + shift) % spec->keyspace;
    }

    pb_zipf_destroy(&zipf, allocator);
    return limit;
}

size_t pb_cache_trace_generate(const pb_cache_trace_spec *spec, uint32_t *out, size_t max_events,
                               const pb_allocator *allocator)
{
    size_t limit;

    assert(spec != NULL);
    assert(out != NULL);

    limit = max_events;
    if (limit > (size_t)spec->events) {
        limit = (size_t)spec->events;
    }
    if (limit == 0u) {
        return 0;
    }

    switch (spec->kind) {
    case PB_CACHE_TRACE_ZIPF:
        return generate_zipf(spec, out, limit, allocator);
    case PB_CACHE_TRACE_SCAN_HEAVY:
        return generate_scan_heavy(spec, out, limit, allocator);
    case PB_CACHE_TRACE_SHIFTING:
        return generate_shifting(spec, out, limit, allocator);
    default:
        return 0;
    }
}

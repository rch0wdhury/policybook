/*
 * Canonical traces for the cache domain.
 *
 * The C side of `packages/core/src/domains/cache/traces.ts`, specified in
 * `packages/core/src/domains/cache/TRACES.md`. The generated parity test checks
 * the first 10,000 events of each trace against the committed reference, event
 * for event.
 *
 * Nothing here reads a file, a clock, or an environment variable.
 */

#ifndef POLICYBOOK_CACHE_TRACES_H
#define POLICYBOOK_CACHE_TRACES_H

#include <stddef.h>
#include <stdint.h>

#include "policybook/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pb_cache_trace_kind {
    PB_CACHE_TRACE_ZIPF,
    PB_CACHE_TRACE_SCAN_HEAVY,
    PB_CACHE_TRACE_SHIFTING
} pb_cache_trace_kind;

/* Everything needed to reproduce a trace and to benchmark against it. */
typedef struct pb_cache_trace_spec {
    const char *id;
    pb_cache_trace_kind kind;
    double alpha;          /* Zipf exponent: 1.0 or 0.75 */
    uint32_t keyspace;     /* distinct keys the Zipf body draws from */
    uint32_t key_universe; /* exclusive upper bound on any key emitted */
    uint32_t capacity;     /* cache capacity this trace is benchmarked at */
    uint32_t events;       /* total events the full trace emits */
    uint32_t seed;
} pb_cache_trace_spec;

/* The four canonical cache traces. */
extern const pb_cache_trace_spec pb_cache_traces[];

#define PB_CACHE_TRACE_COUNT 4

/* Look a trace up by id, or NULL if there is no such trace. */
const pb_cache_trace_spec *pb_cache_trace_find(const char *id);

/*
 * Generate a trace into a caller-provided buffer.
 *
 * Writes at most `max_events` keys and returns how many were written. The
 * prefix of a trace is identical to the full trace, so a short run is still
 * reproducible.
 *
 * `allocator` is used for the Zipf table and released before returning; NULL
 * selects malloc/free. Returns 0 if the table could not be allocated.
 */
size_t pb_cache_trace_generate(const pb_cache_trace_spec *spec, uint32_t *out, size_t max_events,
                               const pb_allocator *allocator);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_TRACES_H */

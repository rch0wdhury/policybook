/*
 * Canonical traces for the rate-limiter domain.
 *
 * The C side of `packages/core/src/domains/rate-limiter/traces.ts`, specified
 * in `packages/core/src/domains/rate-limiter/TRACES.md`. The generated parity
 * test checks the first 10,000 events of each trace against the committed
 * reference, event for event.
 *
 * Arrivals are a per-millisecond Bernoulli process rather than a Poisson one
 *: a Poisson inter-arrival time needs `log`,
 * which is not correctly rounded across C standard libraries, so the three
 * ports would eventually disagree.
 *
 * Nothing here reads a file, a clock, or an environment variable.
 */

#ifndef POLICYBOOK_RATE_LIMITER_TRACES_H
#define POLICYBOOK_RATE_LIMITER_TRACES_H

#include <stddef.h>
#include <stdint.h>

#include "policybook/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pb_ratelimiter_trace_kind {
    /* One key, one Bernoulli trial per millisecond. `steady` and `overload`
     * share this generator and differ only in `arrival_p`. */
    PB_RATELIMITER_TRACE_SINGLE_KEY,
    PB_RATELIMITER_TRACE_BURSTY,
    PB_RATELIMITER_TRACE_MANY_KEYS
} pb_ratelimiter_trace_kind;

/* Everything needed to reproduce a trace and to benchmark against it. */
typedef struct pb_ratelimiter_trace_spec {
    const char *id;
    pb_ratelimiter_trace_kind kind;
    uint32_t duration_ms;  /* length of the simulated period */
    uint32_t key_universe; /* exclusive upper bound on any key emitted */
    uint32_t keyspace;     /* distinct keys the Zipf body draws from, or 0 */
    double arrival_p;      /* Bernoulli probability per millisecond */
    uint32_t seed;
} pb_ratelimiter_trace_spec;

/* The three canonical rate-limiter traces. */
extern const pb_ratelimiter_trace_spec pb_ratelimiter_traces[];

#define PB_RATELIMITER_TRACE_COUNT 4

/* Look a trace up by id, or NULL if there is no such trace. */
const pb_ratelimiter_trace_spec *pb_ratelimiter_trace_find(const char *id);

/*
 * Generate a trace into caller-provided buffers.
 *
 * Writes at most `max_events` arrivals into `times` and `keys` — both must have
 * room for `max_events` — and returns how many were written. That count is the
 * outcome of a Bernoulli process, so it is not known in advance and may be
 * fewer than `max_events` even for the full trace: `steady` produces 5,491
 * arrivals and `bursty` 3,020. Generation consumes the random stream in order,
 * so a truncated trace is exactly a prefix of the full one.
 *
 * `allocator` is used for the Zipf table and released before returning; NULL
 * selects malloc/free. Returns 0 if the table could not be allocated.
 */
size_t pb_ratelimiter_trace_generate(const pb_ratelimiter_trace_spec *spec, uint32_t *times,
                                     uint32_t *keys, size_t max_events,
                                     const pb_allocator *allocator);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_TRACES_H */

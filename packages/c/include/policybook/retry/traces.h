/*
 * Canonical workloads for the retry domain.
 *
 * The C side of `packages/core/src/domains/retry/traces.ts`, specified in
 * `packages/core/src/domains/retry/TRACES.md`. The generated parity test checks
 * the outage duration of every episode against the committed reference.
 *
 * A retry "trace" is not a stream of events but a set of independent episodes:
 * something broke, and a client keeps trying until it succeeds or gives up.
 * What varies between episodes is how long the outage lasts, so the trace is
 * the sequence of outage durations — the first draw of each episode's
 * environment stream, and so exactly what a harness would see.
 *
 * Nothing here reads a file, a clock, or an environment variable.
 */

#ifndef POLICYBOOK_RETRY_TRACES_H
#define POLICYBOOK_RETRY_TRACES_H

#include <stddef.h>
#include <stdint.h>

#include "policybook/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Everything needed to reproduce an episode set. */
typedef struct pb_retry_trace_spec {
    const char *id;
    uint32_t episodes;      /* independent episodes simulated */
    uint32_t max_outage_ms; /* exclusive upper bound on an outage */
    uint32_t deadline_ms;   /* an episode is abandoned past this */
    uint32_t flake_percent; /* chance an attempt after recovery still fails */
    uint32_t seed;
} pb_retry_trace_spec;

/* The canonical retry workload. */
extern const pb_retry_trace_spec pb_retry_traces[];

#define PB_RETRY_TRACE_COUNT 1

/* Look a trace up by id, or NULL if there is no such trace. */
const pb_retry_trace_spec *pb_retry_trace_find(const char *id);

/*
 * Seed for an episode's environment stream: the outage and the success rolls.
 *
 * One stream per episode rather than one for the whole run, so episode 40 faces
 * the same outage whatever the policy did in episode 39.
 */
uint32_t pb_retry_environment_seed(const pb_retry_trace_spec *spec, uint32_t episode);

/*
 * Seed for an episode's policy stream: whatever jitter the policy draws.
 *
 * Deliberately separate from the environment's. A shared stream would let a
 * policy's own draws shift the outcome of the environment's later coin flips,
 * so two policies would face different luck rather than different strategies.
 */
uint32_t pb_retry_policy_seed(const pb_retry_trace_spec *spec, uint32_t episode);

/*
 * Write the outage duration of each episode into `out`.
 *
 * Writes at most `max_events` and returns how many were written. `allocator` is
 * unused and accepted for symmetry with the other domains' generators.
 */
size_t pb_retry_trace_generate(const pb_retry_trace_spec *spec, uint32_t *out,
                               size_t max_events, const pb_allocator *allocator);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RETRY_TRACES_H */

#include "policybook/retry/traces.h"

#include <string.h>

#include "policybook/rng.h"

/* Far enough from any environment seed that the two never collide. */
#define PB_RETRY_POLICY_SEED_OFFSET 1000000u

const pb_retry_trace_spec pb_retry_traces[] = {
    { "outage-30s", 1000u, 30001u, 60000u, 10u, 60u }
};

const pb_retry_trace_spec *pb_retry_trace_find(const char *id)
{
    size_t i;

    if (id == NULL) {
        return NULL;
    }
    for (i = 0; i < (size_t)PB_RETRY_TRACE_COUNT; ++i) {
        if (strcmp(pb_retry_traces[i].id, id) == 0) {
            return &pb_retry_traces[i];
        }
    }
    return NULL;
}

uint32_t pb_retry_environment_seed(const pb_retry_trace_spec *spec, uint32_t episode)
{
    return spec->seed + episode;
}

uint32_t pb_retry_policy_seed(const pb_retry_trace_spec *spec, uint32_t episode)
{
    return spec->seed + PB_RETRY_POLICY_SEED_OFFSET + episode;
}

size_t pb_retry_trace_generate(const pb_retry_trace_spec *spec, uint32_t *out,
                               size_t max_events, const pb_allocator *allocator)
{
    size_t limit;
    size_t episode;

    (void)allocator; /* nothing here allocates */

    if (spec == NULL || out == NULL) {
        return 0;
    }

    limit = (size_t)spec->episodes;
    if (max_events < limit) {
        limit = max_events;
    }

    for (episode = 0; episode < limit; ++episode) {
        pb_rng rng;
        pb_rng_init(&rng, pb_retry_environment_seed(spec, (uint32_t)episode));
        out[episode] = pb_rng_next_int(&rng, spec->max_outage_ms);
    }
    return limit;
}

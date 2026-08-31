/*
 * A token bucket over the bursty trace — rate limiting in use.
 *
 * Build (against the installed library):
 *     cc -std=c99 rate_limiter_token_bucket.c -lpolicybook -lm -o rate_limiter_token_bucket
 *
 * Two things distinguish this interface from most rate limiters you will meet.
 *
 * The caller supplies the time. A limiter that read the clock itself could not
 * be tested, could not be replayed, and could not be run faster than real time
 * — so `now_ms` is a parameter, and this program hands it the trace's own
 * arrival times rather than anything from the system.
 *
 * And `allow` is a decision *and* a commitment: if it returns true the permit
 * has already been spent by the time you see the answer. There is no separate
 * "take it" call, so a limiter can never be asked speculatively.
 */

#include <stdio.h>
#include <stdlib.h>

#include "policybook/rate_limiter/rate_limiter.h"
#include "policybook/rate_limiter/token_bucket.h"
#include "policybook/rate_limiter/traces.h"

int main(void)
{
    /* `bursty`: sixty seconds of traffic arriving in 200 ms bursts separated by
     * 1.8 s of quiet. A token bucket is the policy that handles this shape
     * well — it saves up permits during the quiet and spends them on the
     * burst, which is exactly what a leaky bucket refuses to do. */
    const pb_ratelimiter_trace_spec *trace = pb_ratelimiter_trace_find("bursty");
    pb_ratelimiter_token_bucket_params params = PB_RATELIMITER_TOKEN_BUCKET_PARAMS_DEFAULT;
    pb_ratelimiter *limiter;
    uint32_t *times;
    uint32_t *keys;
    size_t capacity;
    size_t produced;
    size_t i;
    size_t admitted = 0;

    if (trace == NULL) {
        fprintf(stderr, "no such trace\n");
        return 1;
    }

    /* An arrival is the outcome of a per-millisecond coin flip, so the count is
     * not known in advance; the duration is the ceiling. */
    capacity = (size_t)trace->duration_ms;
    times = (uint32_t *)malloc(capacity * sizeof(uint32_t));
    keys = (uint32_t *)malloc(capacity * sizeof(uint32_t));
    if (times == NULL || keys == NULL) {
        fprintf(stderr, "out of memory\n");
        free(times);
        free(keys);
        return 1;
    }

    produced = pb_ratelimiter_trace_generate(trace, times, keys, capacity, NULL);

    /* The domain's reference configuration: 100 permits a second, and a burst
     * allowance of 100 on top. Every policy in the domain is benchmarked at
     * this budget, which is what makes their numbers comparable. */
    params.rate_per_sec = 100u;
    params.burst = 100u;
    params.max_keys = 1024u;
    limiter = pb_ratelimiter_token_bucket.create(&params, NULL, NULL);
    if (limiter == NULL) {
        fprintf(stderr, "could not create the policy\n");
        free(times);
        free(keys);
        return 1;
    }

    for (i = 0; i < produced; ++i) {
        /* Cost is in permits: one request, one permit here, but an API that
         * charges by payload size would pass something else. */
        if (pb_ratelimiter_token_bucket.allow(limiter, (uint64_t)keys[i], 1u,
                                              (uint64_t)times[i])) {
            admitted += 1;
        }
    }

    printf("token bucket on %s: %.4f accept rate over %lu arrivals\n", trace->id,
           (double)admitted / (double)produced, (unsigned long)produced);

    /* What a refused caller should be told. `retry_after` describes the *next*
     * request, and waiting exactly that long is guaranteed to admit a minimal
     * one — an honest number a client can act on, rather than a guess. */
    {
        uint64_t last = produced > 0 ? (uint64_t)times[produced - 1] : 0;
        uint64_t wait = pb_ratelimiter_token_bucket.retry_after(limiter, (uint64_t)keys[0], last);
        printf("  at t=%lu ms, key %lu should come back in %lu ms\n", (unsigned long)last,
               (unsigned long)keys[0], (unsigned long)wait);
    }

    pb_ratelimiter_token_bucket.destroy(limiter);
    free(times);
    free(keys);
    return 0;
}

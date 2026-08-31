#include "fuzz_ratelimiter_core.h"

#include <stdbool.h>
#include <stdio.h>

#include "policybook/rate_limiter/dual_bucket.h"
#include "policybook/rate_limiter/fixed_window.h"
#include "policybook/rate_limiter/gcra.h"
#include "policybook/rate_limiter/leaky_bucket.h"
#include "policybook/rate_limiter/rate_limiter.h"
#include "policybook/rate_limiter/sliding_counter.h"
#include "policybook/rate_limiter/sliding_log.h"
#include "policybook/rate_limiter/token_bucket.h"

/* Reported violations are capped: a broken policy fails on nearly every input,
 * and the first negative test produced hundreds of identical lines that buried
 * the one that mattered (the lesson recorded in T18). */
#define PB_FUZZ_RL_MAX_REPORTS 5

/*
 * Every policy, built from one decoded configuration.
 *
 * The two per-second parameters are reused across policies whose names differ:
 * a `limit`/`window_ms` pair for the window policies and a `rate`/`burst` pair
 * for the buckets are the same two numbers wearing different labels.
 */
typedef struct fuzz_policy {
    const char *name;
    const pb_ratelimiter_vtable *vtable;
    pb_ratelimiter *(*build)(uint32_t rate, uint32_t size, uint32_t max_keys);
} fuzz_policy;

static pb_ratelimiter *build_fixed_window(uint32_t rate, uint32_t size, uint32_t max_keys)
{
    pb_ratelimiter_fixed_window_params params;
    params.limit = size;
    params.window_ms = 1000u / (rate == 0u ? 1u : rate) + 1u;
    params.max_keys = max_keys;
    return pb_ratelimiter_fixed_window.create(&params, NULL, NULL);
}

static pb_ratelimiter *build_sliding_log(uint32_t rate, uint32_t size, uint32_t max_keys)
{
    pb_ratelimiter_sliding_log_params params;
    params.limit = size;
    params.window_ms = 1000u / (rate == 0u ? 1u : rate) + 1u;
    params.max_keys = max_keys;
    return pb_ratelimiter_sliding_log.create(&params, NULL, NULL);
}

static pb_ratelimiter *build_sliding_counter(uint32_t rate, uint32_t size, uint32_t max_keys)
{
    pb_ratelimiter_sliding_counter_params params;
    params.limit = size;
    params.window_ms = 1000u / (rate == 0u ? 1u : rate) + 1u;
    params.max_keys = max_keys;
    return pb_ratelimiter_sliding_counter.create(&params, NULL, NULL);
}

static pb_ratelimiter *build_token_bucket(uint32_t rate, uint32_t size, uint32_t max_keys)
{
    pb_ratelimiter_token_bucket_params params;
    params.rate_per_sec = rate;
    params.burst = size;
    params.max_keys = max_keys;
    return pb_ratelimiter_token_bucket.create(&params, NULL, NULL);
}

static pb_ratelimiter *build_leaky_bucket(uint32_t rate, uint32_t size, uint32_t max_keys)
{
    pb_ratelimiter_leaky_bucket_params params;
    params.rate_per_sec = rate;
    params.capacity = size;
    params.max_keys = max_keys;
    return pb_ratelimiter_leaky_bucket.create(&params, NULL, NULL);
}

static pb_ratelimiter *build_gcra(uint32_t rate, uint32_t size, uint32_t max_keys)
{
    pb_ratelimiter_gcra_params params;
    params.rate_per_sec = rate;
    params.burst = size;
    params.max_keys = max_keys;
    return pb_ratelimiter_gcra.create(&params, NULL, NULL);
}

static pb_ratelimiter *build_dual_bucket(uint32_t rate, uint32_t size, uint32_t max_keys)
{
    pb_ratelimiter_dual_bucket_params params;
    /* Per-minute ceilings, so the same rate expressed sixty times over. */
    params.requests_per_min = rate * 60u;
    params.tokens_per_min = size * 60u;
    params.max_keys = max_keys;
    return pb_ratelimiter_dual_bucket.create(&params, NULL, NULL);
}

static const fuzz_policy POLICIES[] = {
    { "fixed-window", &pb_ratelimiter_fixed_window, build_fixed_window },
    { "sliding-log", &pb_ratelimiter_sliding_log, build_sliding_log },
    { "sliding-counter", &pb_ratelimiter_sliding_counter, build_sliding_counter },
    { "token-bucket", &pb_ratelimiter_token_bucket, build_token_bucket },
    { "leaky-bucket", &pb_ratelimiter_leaky_bucket, build_leaky_bucket },
    { "gcra", &pb_ratelimiter_gcra, build_gcra },
    { "dual-bucket", &pb_ratelimiter_dual_bucket, build_dual_bucket }
};

#define POLICY_COUNT (sizeof(POLICIES) / sizeof(POLICIES[0]))

unsigned pb_fuzz_ratelimiter_policy_count(void)
{
    return (unsigned)POLICY_COUNT;
}

static void report(int *violations, const char *policy, const char *message)
{
    *violations += 1;
    if (*violations <= PB_FUZZ_RL_MAX_REPORTS) {
        fprintf(stderr, "FUZZ %s: %s\n", policy, message);
    }
}

/*
 * Drive one policy through a decoded sequence.
 *
 * The first four bytes configure the policy; the rest are read three at a time
 * as (key, cost, time-step). Anything the decoder cannot use is simply not
 * read, so a truncated input is a shorter run rather than an error.
 */
int pb_fuzz_ratelimiter_once(const uint8_t *data, size_t size)
{
    size_t index;
    int violations = 0;

    if (size < 5u) {
        return 0;
    }

    {
        const fuzz_policy *entry = &POLICIES[data[0] % POLICY_COUNT];
        /* Small but never zero: a rate of 0 or a size of 0 is refused at create
         * by every policy, and testing that is the unit tests' job. */
        uint32_t rate = 1u + (uint32_t)(data[1] % 200u);
        uint32_t bucket = 1u + (uint32_t)(data[2] % 32u);
        uint32_t max_keys = 1u + (uint32_t)(data[3] % PB_FUZZ_RL_KEY_SPACE);
        pb_ratelimiter *limiter = entry->build(rate, bucket, max_keys);
        size_t memory_after_create;
        uint64_t now = 0;

        if (limiter == NULL) {
            /* A configuration a policy legitimately refuses. Not a violation. */
            return 0;
        }
        memory_after_create = entry->vtable->memory_bytes(limiter);

        for (index = 4u; index + 2u < size; index += 3u) {
            uint64_t key = (uint64_t)(data[index] % PB_FUZZ_RL_KEY_SPACE);
            uint32_t cost = 1u + (uint32_t)(data[index + 1u] % 4u);
            uint64_t wait;

            /* Time only moves forward: the interface promises non-decreasing
             * `now_ms`, and a policy handed a clock that went backwards is
             * entitled to do anything. */
            now += (uint64_t)data[index + 2u];

            /* Whether it went through is the policy's business; what it says
             * about the next one is the invariant. */
            (void)entry->vtable->allow(limiter, key, cost, now);

            /*
             * The honesty check, applied whatever `allow` decided.
             *
             * `retry_after` describes the *next* request, not the one that just
             * happened — a request that consumed the last permit leaves a
             * positive wait behind it, which is correct rather than a defect.
             * So the invariant is simply: whatever number comes back, waiting
             * that long must admit a minimal request. A zero says "now", and
             * that has to be true too.
             */
            wait = entry->vtable->retry_after(limiter, key, now);

            if (wait == PB_RATELIMITER_RETRY_UNKNOWN) {
                /* The policy has declined to answer, which it may only do when
                 * the key table is full and this key will never be tracked.
                 * Then the refusal has to be permanent — a policy that says "I
                 * cannot tell you" and then admits the next request was simply
                 * wrong. */
                if (entry->vtable->state_size(limiter) < (size_t)max_keys) {
                    report(&violations, entry->name,
                           "retry_after returned UNKNOWN with room left in the table");
                }
                if (entry->vtable->allow(limiter, key, 1u, now)) {
                    report(&violations, entry->name,
                           "retry_after returned UNKNOWN but the key was then admitted");
                }
            } else {
                /* A *minimal* request: the one just refused may have cost more
                 * than one unit, which no `retry_after` signature can express. */
                if (!entry->vtable->allow(limiter, key, 1u, now + wait)) {
                    report(&violations, entry->name,
                           "retry_after elapsed and a minimal request was still refused");
                }
                now += wait;
            }

            if (entry->vtable->state_size(limiter) > (size_t)max_keys) {
                report(&violations, entry->name, "state_size exceeded max_keys");
            }
            if (entry->vtable->memory_bytes(limiter) != memory_after_create) {
                report(&violations, entry->name, "memory grew after create");
            }
        }

        entry->vtable->destroy(limiter);
    }

    return violations;
}

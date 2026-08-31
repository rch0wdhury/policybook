/*
 * What the shared vectors cannot check about the C rate limiters.
 *
 * The vectors are language-neutral, so they can only exercise behaviour all
 * three ports share. Two things here are C-only and would otherwise ship
 * untested:
 *
 *   1. `max_keys`. TypeScript and Python grow a hash map without limit; C takes
 *      all its memory in create and never allocates again (concept.md §12.2),
 *      so the number of tracked keys is bounded up front and a key beyond that
 *      bound is refused. That is a deliberate fail-closed choice — letting an
 *      unknown key through would mean an attacker who cycles keys turns the
 *      limiter off — and it needs a test saying so.
 *
 *   2. The no-allocation-after-create contract, checked with a counting
 *      allocator rather than trusted.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "policybook/allocator.h"
#include "policybook/rate_limiter/dual_bucket.h"
#include "policybook/rate_limiter/fixed_window.h"
#include "policybook/rate_limiter/gcra.h"
#include "policybook/rate_limiter/leaky_bucket.h"
#include "policybook/rate_limiter/rate_limiter.h"
#include "policybook/rate_limiter/sliding_counter.h"
#include "policybook/rate_limiter/sliding_log.h"
#include "policybook/rate_limiter/token_bucket.h"

#include "pb_test.h"

/* --- a counting allocator, to prove nothing allocates after create --------- */

typedef struct counting_allocator {
    int allocations;
    int frees;
    size_t live_bytes;
} counting_allocator;

static void *counting_alloc(void *ctx, size_t n)
{
    counting_allocator *state = (counting_allocator *)ctx;
    state->allocations += 1;
    state->live_bytes += n;
    return pb_allocator_default()->alloc(NULL, n);
}

static void counting_free(void *ctx, void *p, size_t n)
{
    counting_allocator *state = (counting_allocator *)ctx;
    state->frees += 1;
    state->live_bytes -= n;
    pb_allocator_default()->free(NULL, p, n);
}

/* --- the three limiters, driven through one vtable ------------------------- */

typedef struct limiter_case {
    const char *name;
    const pb_ratelimiter_vtable *vtable;
    const void *params;
} limiter_case;

static const pb_ratelimiter_fixed_window_params fixed_window_params = { 5u, 1000u, 4u };
static const pb_ratelimiter_sliding_log_params sliding_log_params = { 5u, 1000u, 4u };
static const pb_ratelimiter_sliding_counter_params sliding_counter_params = { 5u, 1000u, 4u };
/* rate 5/s, size 5, four keys — the buckets are configured to match the
 * windows so every limiter below admits five and then refuses. */
static const pb_ratelimiter_token_bucket_params token_bucket_params = { 5u, 5u, 4u };
static const pb_ratelimiter_leaky_bucket_params leaky_bucket_params = { 5u, 5u, 4u };
static const pb_ratelimiter_gcra_params gcra_params = { 5u, 5u, 4u };
/* Five calls a minute and five work units a minute, so a cost-1 call is
 * limited by both dimensions at once and the suite below reads the same as it
 * does for the single-dimension policies. */
static const pb_ratelimiter_dual_bucket_params dual_bucket_params = { 5u, 5u, 4u };

static const limiter_case CASES[] = {
    { "fixed-window", &pb_ratelimiter_fixed_window, &fixed_window_params },
    { "sliding-log", &pb_ratelimiter_sliding_log, &sliding_log_params },
    { "sliding-counter", &pb_ratelimiter_sliding_counter, &sliding_counter_params },
    { "token-bucket", &pb_ratelimiter_token_bucket, &token_bucket_params },
    { "leaky-bucket", &pb_ratelimiter_leaky_bucket, &leaky_bucket_params },
    { "gcra", &pb_ratelimiter_gcra, &gcra_params },
    { "dual-bucket", &pb_ratelimiter_dual_bucket, &dual_bucket_params }
};

#define CASE_COUNT (sizeof(CASES) / sizeof(CASES[0]))

/*
 * A key beyond `max_keys` is refused, and the keys already tracked are not
 * disturbed by the attempt.
 */
static void test_max_keys_fails_closed(void)
{
    size_t i;

    for (i = 0; i < CASE_COUNT; ++i) {
        const pb_ratelimiter_vtable *v = CASES[i].vtable;
        pb_ratelimiter *limiter = v->create(CASES[i].params, NULL, NULL);
        uint64_t key;

        PB_CHECK(limiter != NULL);
        if (limiter == NULL) {
            continue;
        }

        /* Four keys fit. */
        for (key = 0; key < 4u; ++key) {
            PB_CHECK(v->allow(limiter, key, 1u, 0u));
        }
        PB_CHECK_U64((uint64_t)v->state_size(limiter), 4u);

        /* The fifth does not, and is refused rather than let through. */
        PB_CHECK(!v->allow(limiter, 99u, 1u, 0u));
        PB_CHECK_U64((uint64_t)v->state_size(limiter), 4u);

        /* A key already tracked still works, and still has its own budget. */
        PB_CHECK(v->allow(limiter, 0u, 4u, 0u));
        PB_CHECK(!v->allow(limiter, 0u, 1u, 0u));

        v->destroy(limiter);
    }
}

/* Everything is taken in create; the hot path allocates nothing. */
static void test_no_allocation_after_create(void)
{
    size_t i;

    for (i = 0; i < CASE_COUNT; ++i) {
        counting_allocator counter = { 0, 0, 0 };
        pb_allocator allocator;
        const pb_ratelimiter_vtable *v = CASES[i].vtable;
        pb_ratelimiter *limiter;
        int after_create;
        uint64_t now;

        allocator.alloc = counting_alloc;
        allocator.free = counting_free;
        allocator.ctx = &counter;

        limiter = v->create(CASES[i].params, &allocator, NULL);
        PB_CHECK(limiter != NULL);
        if (limiter == NULL) {
            continue;
        }

        after_create = counter.allocations;
        PB_CHECK(after_create > 0);

        for (now = 0; now < 20000u; now += 7u) {
            (void)v->allow(limiter, now % 4u, 1u, now);
            (void)v->retry_after(limiter, now % 4u, now);
            (void)v->state_size(limiter);
        }

        /* Not one allocation in ~2,800 decisions across twenty windows. */
        PB_CHECK(counter.allocations == after_create);

        v->destroy(limiter);
        PB_CHECK(counter.live_bytes == 0);
        PB_CHECK(counter.frees == counter.allocations);
    }
}

/* Invalid parameters produce NULL rather than a limiter that cannot work. */
static void test_rejects_bad_params(void)
{
    pb_ratelimiter_fixed_window_params params = PB_RATELIMITER_FIXED_WINDOW_PARAMS_DEFAULT;

    params.limit = 0u;
    PB_CHECK(pb_ratelimiter_fixed_window.create(&params, NULL, NULL) == NULL);

    params = (pb_ratelimiter_fixed_window_params)PB_RATELIMITER_FIXED_WINDOW_PARAMS_DEFAULT;
    params.window_ms = 0u;
    PB_CHECK(pb_ratelimiter_fixed_window.create(&params, NULL, NULL) == NULL);

    params = (pb_ratelimiter_fixed_window_params)PB_RATELIMITER_FIXED_WINDOW_PARAMS_DEFAULT;
    params.max_keys = 0u;
    PB_CHECK(pb_ratelimiter_fixed_window.create(&params, NULL, NULL) == NULL);

    /* NULL params means "use the defaults", not "fail". */
    {
        pb_ratelimiter *limiter = pb_ratelimiter_fixed_window.create(NULL, NULL, NULL);
        PB_CHECK(limiter != NULL);
        pb_ratelimiter_fixed_window.destroy(limiter);
    }

    /* destroy(NULL) is safe. */
    pb_ratelimiter_fixed_window.destroy(NULL);
}

/*
 * `retry_after` is honest: waiting exactly that long admits.
 *
 * The same property that found a real bug in the TypeScript sliding counter
 * (PROGRESS.md, T23 part 1). It is re-checked here because the C port computes
 * it with unsigned arithmetic, where an off-by-one underflows into an enormous
 * wait rather than a small wrong one.
 */
static void test_retry_after_is_honest(void)
{
    size_t i;

    for (i = 0; i < CASE_COUNT; ++i) {
        const pb_ratelimiter_vtable *v = CASES[i].vtable;
        pb_ratelimiter *limiter = v->create(CASES[i].params, NULL, NULL);
        uint64_t now = 0;
        int denials = 0;
        int step;

        PB_CHECK(limiter != NULL);
        if (limiter == NULL) {
            continue;
        }

        for (step = 0; step < 400; ++step) {
            now += (uint64_t)(step % 173);
            if (v->allow(limiter, 1u, 1u, now)) {
                continue;
            }
            denials += 1;
            {
                uint64_t wait = v->retry_after(limiter, 1u, now);
                /* A wait never exceeds one refill period. Most policies here
                 * work in seconds, but the dual bucket's ceilings are stated
                 * per minute — five calls a minute is a twelve-second wait —
                 * so the bound is a minute, not the second it first assumed. */
                PB_CHECK(wait <= 60u * 1000u);
                PB_CHECK(v->allow(limiter, 1u, 1u, now + wait));
                now += wait;
            }
        }

        /* Guards the guard: a run with no denials would pass vacuously. */
        PB_CHECK(denials > 20);
        v->destroy(limiter);
    }
}

/*
 * The token bucket and the leaky bucket are the same algorithm in C too.
 *
 * The vectors pin each policy's own numbers, and reading the two files side by
 * side shows they mirror — but only this checks the substitution
 * `tokens = capacity - level` holds across a long run, which is where a
 * divergence in the saturation handling would appear.
 */
static void test_buckets_are_duals(void)
{
    pb_ratelimiter_token_bucket_params tb_params = { 37u, 6u, 4u };
    pb_ratelimiter_leaky_bucket_params lb_params = { 37u, 6u, 4u };
    pb_ratelimiter *tokens = pb_ratelimiter_token_bucket.create(&tb_params, NULL, NULL);
    pb_ratelimiter *level = pb_ratelimiter_leaky_bucket.create(&lb_params, NULL, NULL);
    uint64_t now = 0;
    int step;
    int denials = 0;

    PB_CHECK(tokens != NULL && level != NULL);
    if (tokens == NULL || level == NULL) {
        pb_ratelimiter_token_bucket.destroy(tokens);
        pb_ratelimiter_leaky_bucket.destroy(level);
        return;
    }

    for (step = 0; step < 600; ++step) {
        uint32_t cost = (uint32_t)(step % 3) + 1u;
        bool by_tokens;
        bool by_level;

        now += (uint64_t)(step % 47);
        by_tokens = pb_ratelimiter_token_bucket.allow(tokens, 1u, cost, now);
        by_level = pb_ratelimiter_leaky_bucket.allow(level, 1u, cost, now);
        PB_CHECK(by_tokens == by_level);
        if (!by_tokens) {
            denials += 1;
        }
        PB_CHECK_U64(pb_ratelimiter_token_bucket.retry_after(tokens, 1u, now),
                     pb_ratelimiter_leaky_bucket.retry_after(level, 1u, now));
    }

    /* Guards the guard: two limiters that admitted everything would agree
     * trivially. */
    PB_CHECK(denials > 100);

    pb_ratelimiter_token_bucket.destroy(tokens);
    pb_ratelimiter_leaky_bucket.destroy(level);
}

/*
 * GCRA reaches the token bucket's decisions from a third of the state.
 *
 * The vectors pin each policy's own numbers; this checks the agreement holds
 * across a long run, which is where a divergence in how the two cap an idle
 * key's credit would show up.
 */
static void test_gcra_matches_token_bucket(void)
{
    pb_ratelimiter_token_bucket_params tb_params = { 37u, 6u, 4u };
    pb_ratelimiter_gcra_params gcra_config = { 37u, 6u, 4u };
    pb_ratelimiter *tokens = pb_ratelimiter_token_bucket.create(&tb_params, NULL, NULL);
    pb_ratelimiter *gcra = pb_ratelimiter_gcra.create(&gcra_config, NULL, NULL);
    uint64_t now = 0;
    int step;
    int denials = 0;

    PB_CHECK(tokens != NULL && gcra != NULL);
    if (tokens == NULL || gcra == NULL) {
        pb_ratelimiter_token_bucket.destroy(tokens);
        pb_ratelimiter_gcra.destroy(gcra);
        return;
    }

    for (step = 0; step < 600; ++step) {
        uint32_t cost = (uint32_t)(step % 3) + 1u;
        bool by_tokens;
        bool by_gcra;

        now += (uint64_t)(step % 47);
        by_tokens = pb_ratelimiter_token_bucket.allow(tokens, 1u, cost, now);
        by_gcra = pb_ratelimiter_gcra.allow(gcra, 1u, cost, now);
        PB_CHECK(by_tokens == by_gcra);
        if (!by_tokens) {
            denials += 1;
        }
        PB_CHECK_U64(pb_ratelimiter_token_bucket.retry_after(tokens, 1u, now),
                     pb_ratelimiter_gcra.retry_after(gcra, 1u, now));
    }

    PB_CHECK(denials > 100);

    /* And the reason to prefer it: one integer per key rather than three. */
    PB_CHECK(pb_ratelimiter_gcra.memory_bytes(gcra) <
             pb_ratelimiter_token_bucket.memory_bytes(tokens));

    pb_ratelimiter_token_bucket.destroy(tokens);
    pb_ratelimiter_gcra.destroy(gcra);
}

/* A dual bucket refuses on either dimension, and charges neither when it does. */
static void test_dual_bucket_charges_atomically(void)
{
    pb_ratelimiter_dual_bucket_params params = { 3u, 1000u, 4u };
    pb_ratelimiter *limiter = pb_ratelimiter_dual_bucket.create(&params, NULL, NULL);

    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    /* Key 1: three small calls exhaust the request ceiling, work to spare. */
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 1u, 100u, 0u));
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 1u, 100u, 0u));
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 1u, 100u, 0u));
    PB_CHECK(!pb_ratelimiter_dual_bucket.allow(limiter, 1u, 100u, 0u));
    /* Three a minute is one every twenty seconds. */
    PB_CHECK_U64(pb_ratelimiter_dual_bucket.retry_after(limiter, 1u, 0u), 20000u);

    /* Key 2: one enormous call exhausts the work ceiling, requests to spare. */
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 2u, 1000u, 0u));
    PB_CHECK(!pb_ratelimiter_dual_bucket.allow(limiter, 2u, 1u, 0u));
    /* A thousand a minute is one every sixty milliseconds — a much shorter
     * wait than key 1's, because a different dimension is binding. */
    PB_CHECK_U64(pb_ratelimiter_dual_bucket.retry_after(limiter, 2u, 0u), 60u);

    /* The refusal charged no request: two calls remain, so after the work
     * dimension recovers, two more calls go through and the third does not. */
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 2u, 1u, 60u));
    PB_CHECK(pb_ratelimiter_dual_bucket.allow(limiter, 2u, 1u, 120u));
    PB_CHECK(!pb_ratelimiter_dual_bucket.allow(limiter, 2u, 1u, 180u));

    pb_ratelimiter_dual_bucket.destroy(limiter);
}

/*
 * A zero-cost call is a conformance probe: the reference computes
 * (cost - 1) * UNIT = -UNIT, so it is admitted whenever the next permit is
 * within one unit of accruing — always, after any admitted call — and
 * charges nothing. An unsigned `cost - 1u` would wrap to 2^32 - 1 and refuse
 * a request the reference admits.
 */
static void test_gcra_zero_cost_is_admitted_and_charges_nothing(void)
{
    pb_ratelimiter_gcra_params params = { 1u, 1u, 4u }; /* 1/s, burst 1 */
    pb_ratelimiter *limiter = pb_ratelimiter_gcra.create(&params, NULL, NULL);

    PB_CHECK(limiter != NULL);
    if (limiter == NULL) {
        return;
    }

    /* A fresh key: admitted, and nothing is charged. */
    PB_CHECK(pb_ratelimiter_gcra.allow(limiter, 1u, 0u, 0u));
    PB_CHECK_U64(pb_ratelimiter_gcra.retry_after(limiter, 1u, 0u), 0u);

    /* The burst is intact: a cost-1 call still goes through, and only then is
     * the next permit a full second away. */
    PB_CHECK(pb_ratelimiter_gcra.allow(limiter, 1u, 1u, 0u));
    PB_CHECK(!pb_ratelimiter_gcra.allow(limiter, 1u, 1u, 0u));
    PB_CHECK_U64(pb_ratelimiter_gcra.retry_after(limiter, 1u, 0u), 1000u);

    /* Even now cost 0 is admitted — the -UNIT clears the test exactly — and
     * the TAT does not move: the wait is unchanged and a cost-1 call is still
     * refused. */
    PB_CHECK(pb_ratelimiter_gcra.allow(limiter, 1u, 0u, 0u));
    PB_CHECK_U64(pb_ratelimiter_gcra.retry_after(limiter, 1u, 0u), 1000u);
    PB_CHECK(!pb_ratelimiter_gcra.allow(limiter, 1u, 1u, 0u));
    PB_CHECK_U64((uint64_t)pb_ratelimiter_gcra.state_size(limiter), 1u);

    pb_ratelimiter_gcra.destroy(limiter);
}

int main(void)
{
    test_max_keys_fails_closed();
    test_no_allocation_after_create();
    test_rejects_bad_params();
    test_retry_after_is_honest();
    test_buckets_are_duals();
    test_gcra_matches_token_bucket();
    test_gcra_zero_cost_is_admitted_and_charges_nothing();
    test_dual_bucket_charges_atomically();
    return pb_test_summary("rate-limiter C behaviour");
}

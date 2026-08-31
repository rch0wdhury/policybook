/*
 * What the shared vectors cannot check about the C retry policies.
 *
 * The vectors are language-neutral, so they only exercise behaviour all three
 * ports share. Three things here are C-only:
 *
 *   1. `create` may be handed a NULL `pb_rng`. A jittered policy given one has
 *      to seed itself deterministically rather than reach for a global source,
 *      and `destroy` must still be correct either way.
 *   2. The no-allocation-after-create contract, checked with a counting
 *      allocator rather than trusted.
 *   3. `decorrelated_jitter` refuses a `cap_ms` above UINT32_MAX/3, because its
 *      walk computes `prev * 3` and `prev` is bounded by the cap.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "policybook/allocator.h"
#include "policybook/retry/constant.h"
#include "policybook/retry/decorrelated_jitter.h"
#include "policybook/retry/equal_jitter.h"
#include "policybook/retry/exponential.h"
#include "policybook/retry/exponential_full_jitter.h"
#include "policybook/retry/retry.h"
#include "policybook/retry/retry_after_aware.h"

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

/* --- every policy, driven through one vtable ------------------------------ */

typedef struct policy_case {
    const char *name;
    const pb_retry_vtable *vtable;
    const void *params;
} policy_case;

static const pb_retry_constant_params constant_params = { 100u, 6u };
static const pb_retry_exponential_params exponential_params = { 100u, 10000u, 6u };
static const pb_retry_exponential_full_jitter_params full_jitter_params = { 100u, 10000u, 6u };
static const pb_retry_equal_jitter_params equal_jitter_params = { 100u, 10000u, 6u };
static const pb_retry_decorrelated_jitter_params decorrelated_params = { 100u, 10000u, 6u };
static const pb_retry_retry_after_aware_params aware_params = { 100u, 10000u, 6u };

static const policy_case CASES[] = {
    { "constant", &pb_retry_constant, &constant_params },
    { "exponential", &pb_retry_exponential, &exponential_params },
    { "exponential-full-jitter", &pb_retry_exponential_full_jitter, &full_jitter_params },
    { "equal-jitter", &pb_retry_equal_jitter, &equal_jitter_params },
    { "decorrelated-jitter", &pb_retry_decorrelated_jitter, &decorrelated_params },
    { "retry-after-aware", &pb_retry_retry_after_aware, &aware_params }
};

#define CASE_COUNT (sizeof(CASES) / sizeof(CASES[0]))

/*
 * A NULL rng is accepted, and a jittered policy still produces delays.
 *
 * The alternative — reaching for a global source — would make the policy
 * untestable, so the fallback is a deterministic seed inside the policy's own
 * struct. `destroy` has nothing separate to free either way.
 */
static void test_null_rng_is_accepted(void)
{
    size_t i;

    for (i = 0; i < CASE_COUNT; ++i) {
        const pb_retry_vtable *v = CASES[i].vtable;
        pb_retry *policy = v->create(CASES[i].params, NULL, NULL);
        pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
        int64_t delay;

        PB_CHECK(policy != NULL);
        if (policy == NULL) {
            continue;
        }

        delay = v->next_delay(policy, 1u, &error);
        PB_CHECK(delay >= 0);
        PB_CHECK(delay <= 10000);
        v->destroy(policy);
    }
}

/* Two policies seeded alike agree; two seeded differently mostly do not. */
static void test_seeding_is_deterministic(void)
{
    pb_rng left;
    pb_rng right;
    pb_rng other;
    pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
    pb_retry *a;
    pb_retry *b;
    pb_retry *c;
    int step;
    int agreements = 0;

    pb_rng_init(&left, 42u);
    pb_rng_init(&right, 42u);
    pb_rng_init(&other, 43u);

    a = pb_retry_exponential_full_jitter.create(&full_jitter_params, NULL, &left);
    b = pb_retry_exponential_full_jitter.create(&full_jitter_params, NULL, &right);
    c = pb_retry_exponential_full_jitter.create(&full_jitter_params, NULL, &other);
    PB_CHECK(a != NULL && b != NULL && c != NULL);
    if (a == NULL || b == NULL || c == NULL) {
        return;
    }

    for (step = 1; step <= 5; ++step) {
        int64_t from_a = pb_retry_exponential_full_jitter.next_delay(a, (uint32_t)step, &error);
        int64_t from_b = pb_retry_exponential_full_jitter.next_delay(b, (uint32_t)step, &error);
        int64_t from_c = pb_retry_exponential_full_jitter.next_delay(c, (uint32_t)step, &error);

        PB_CHECK_I64(from_a, from_b);
        if (from_a == from_c) {
            agreements += 1;
        }
    }
    /* A different seed should not shadow the first for five straight draws. */
    PB_CHECK(agreements < 5);

    pb_retry_exponential_full_jitter.destroy(a);
    pb_retry_exponential_full_jitter.destroy(b);
    pb_retry_exponential_full_jitter.destroy(c);
}

/* Everything is taken in create; the hot path allocates nothing. */
static void test_no_allocation_after_create(void)
{
    size_t i;

    for (i = 0; i < CASE_COUNT; ++i) {
        counting_allocator counter = { 0, 0, 0 };
        pb_allocator allocator;
        pb_rng rng;
        pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
        const pb_retry_vtable *v = CASES[i].vtable;
        pb_retry *policy;
        int after_create;
        int step;

        allocator.alloc = counting_alloc;
        allocator.free = counting_free;
        allocator.ctx = &counter;
        pb_rng_init(&rng, 3u);

        policy = v->create(CASES[i].params, &allocator, &rng);
        PB_CHECK(policy != NULL);
        if (policy == NULL) {
            continue;
        }

        after_create = counter.allocations;
        PB_CHECK(after_create > 0);

        for (step = 0; step < 2000; ++step) {
            error.has_retry_after = (step % 3) == 0;
            error.retry_after_ms = (uint64_t)(step % 500);
            (void)v->next_delay(policy, (uint32_t)(step % 8) + 1u, &error);
        }

        PB_CHECK(counter.allocations == after_create);
        v->destroy(policy);
        PB_CHECK(counter.live_bytes == 0);
        PB_CHECK(counter.frees == counter.allocations);
    }
}

/* Invalid parameters produce NULL rather than a policy that cannot work. */
static void test_rejects_bad_params(void)
{
    pb_retry_exponential_params exponential = PB_RETRY_EXPONENTIAL_PARAMS_DEFAULT;
    pb_retry_decorrelated_jitter_params decorrelated =
        PB_RETRY_DECORRELATED_JITTER_PARAMS_DEFAULT;
    pb_retry_constant_params constant = PB_RETRY_CONSTANT_PARAMS_DEFAULT;
    pb_retry *policy;

    exponential.base_ms = 0u;
    PB_CHECK(pb_retry_exponential.create(&exponential, NULL, NULL) == NULL);
    exponential = (pb_retry_exponential_params)PB_RETRY_EXPONENTIAL_PARAMS_DEFAULT;
    exponential.max_attempts = 0u;
    PB_CHECK(pb_retry_exponential.create(&exponential, NULL, NULL) == NULL);

    /* A base of zero is legitimate for `constant` — retrying immediately is
     * aggressive, not invalid — so only the attempt budget has a floor. */
    constant.base_ms = 0u;
    policy = pb_retry_constant.create(&constant, NULL, NULL);
    PB_CHECK(policy != NULL);
    pb_retry_constant.destroy(policy);

    /* The walk computes `prev * 3`, and `prev` is bounded by the cap, so a cap
     * above a third of the range would overflow. Refused at create rather than
     * wrapping silently in the hot path. */
    decorrelated.cap_ms = UINT32_MAX / 3u + 1u;
    PB_CHECK(pb_retry_decorrelated_jitter.create(&decorrelated, NULL, NULL) == NULL);
    decorrelated.cap_ms = UINT32_MAX / 3u;
    policy = pb_retry_decorrelated_jitter.create(&decorrelated, NULL, NULL);
    PB_CHECK(policy != NULL);
    pb_retry_decorrelated_jitter.destroy(policy);

    /* NULL params means "use the defaults", not "fail"; destroy(NULL) is safe. */
    policy = pb_retry_exponential.create(NULL, NULL, NULL);
    PB_CHECK(policy != NULL);
    pb_retry_exponential.destroy(policy);
    pb_retry_exponential.destroy(NULL);
}

/*
 * The decorrelated walk is per-policy state, not per-process.
 *
 * A fresh policy restarts at `base`, which is what makes an episode
 * independent — and what makes a policy built inside a retry loop degenerate.
 */
static void test_decorrelated_walk_is_per_policy(void)
{
    pb_rng rng;
    pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
    pb_retry_decorrelated_jitter_params params = { 100u, 10000u, 500u };
    int64_t first_run[6];
    int64_t second_run[6];
    int step;
    pb_retry *policy;

    pb_rng_init(&rng, 11u);
    policy = pb_retry_decorrelated_jitter.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }
    for (step = 0; step < 6; ++step) {
        first_run[step] = pb_retry_decorrelated_jitter.next_delay(policy, 1u, &error);
        /* Every delay is at least the base and never above the cap. */
        PB_CHECK(first_run[step] >= 100);
        PB_CHECK(first_run[step] <= 10000);
    }
    pb_retry_decorrelated_jitter.destroy(policy);

    /* A new policy on a freshly seeded stream walks the identical path, which
     * is only true if the state lives in the policy rather than anywhere else. */
    pb_rng_init(&rng, 11u);
    policy = pb_retry_decorrelated_jitter.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }
    for (step = 0; step < 6; ++step) {
        second_run[step] = pb_retry_decorrelated_jitter.next_delay(policy, 1u, &error);
        PB_CHECK_I64(second_run[step], first_run[step]);
    }
    pb_retry_decorrelated_jitter.destroy(policy);
}

/* A hinted error is honoured and clamped; an absent hint falls back to a draw. */
static void test_retry_after_is_honoured_and_clamped(void)
{
    pb_rng rng;
    pb_retry_retry_after_aware_params params = { 100u, 2000u, 500u };
    pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
    pb_retry *policy;

    pb_rng_init(&rng, 5u);
    policy = pb_retry_retry_after_aware.create(&params, NULL, &rng);
    PB_CHECK(policy != NULL);
    if (policy == NULL) {
        return;
    }

    error.has_retry_after = true;
    error.retry_after_ms = 1500u;
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 1u, &error), 1500);

    /* Clamped: a server under load can ask for minutes, and `cap_ms` is the
     * caller's statement of how long it will accept being told to wait. */
    error.retry_after_ms = 600000u;
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 1u, &error), 2000);

    /* Zero is an instruction, not an absence. */
    error.retry_after_ms = 0u;
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(policy, 1u, &error), 0);

    /* Absent: falls back to a draw under the attempt-1 ceiling of 100. */
    error.has_retry_after = false;
    {
        int64_t delay = pb_retry_retry_after_aware.next_delay(policy, 1u, &error);
        PB_CHECK(delay >= 0);
        PB_CHECK(delay <= 100);
    }

    pb_retry_retry_after_aware.destroy(policy);
}

/*
 * The backoff ceiling above 2^31.
 *
 * The reference computes min(cap, base * 2^(attempt - 1)) in float64, so a
 * doubling that passes 2^32 still lands on the cap; a 32-bit running delay
 * would wrap instead (3e9 * 2 mod 2^32 is 1705032704). The jittered values
 * are pinned from index.ts driven with Rng(7).
 */
static void test_backoff_ceiling_survives_32_bit_doubling(void)
{
    pb_retry_exponential_params exp_wrap = { 3000000000u, 4000000000u, 8u };
    pb_retry_equal_jitter_params eq_wrap = { 3000000000u, 4000000000u, 8u };
    pb_retry_exponential_full_jitter_params full_wrap = { 3000000000u, 4000000000u, 8u };
    pb_retry_retry_after_aware_params aware_wrap = { 3000000000u, 4000000000u, 8u };
    pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
    pb_rng rng_eq;
    pb_rng rng_full;
    pb_rng rng_aware;
    pb_retry *exp;
    pb_retry *eq;
    pb_retry *full;
    pb_retry *aware;

    pb_rng_init(&rng_eq, 7u);
    pb_rng_init(&rng_full, 7u);
    pb_rng_init(&rng_aware, 7u);
    exp = pb_retry_exponential.create(&exp_wrap, NULL, NULL);
    eq = pb_retry_equal_jitter.create(&eq_wrap, NULL, &rng_eq);
    full = pb_retry_exponential_full_jitter.create(&full_wrap, NULL, &rng_full);
    aware = pb_retry_retry_after_aware.create(&aware_wrap, NULL, &rng_aware);
    PB_CHECK(exp != NULL && eq != NULL && full != NULL && aware != NULL);
    if (exp == NULL || eq == NULL || full == NULL || aware == NULL) {
        return;
    }

    /* Attempt 2 doubles 3e9 past 2^32; the answer is the cap, not the wrap. */
    PB_CHECK_I64(pb_retry_exponential.next_delay(exp, 1u, &error), 3000000000);
    PB_CHECK_I64(pb_retry_exponential.next_delay(exp, 2u, &error), 4000000000);
    PB_CHECK_I64(pb_retry_exponential.next_delay(exp, 3u, &error), 4000000000);

    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(eq, 1u, &error), 2493689118);
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(eq, 2u, &error), 2089369888);
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(eq, 3u, &error), 3706118055);

    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(full, 1u, &error), 993689119);
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(full, 2u, &error), 89369889);
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(full, 3u, &error), 1706118055);

    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(aware, 1u, &error), 993689119);
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(aware, 2u, &error), 89369889);
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(aware, 3u, &error), 1706118055);

    pb_retry_exponential.destroy(exp);
    pb_retry_equal_jitter.destroy(eq);
    pb_retry_exponential_full_jitter.destroy(full);
    pb_retry_retry_after_aware.destroy(aware);
}

/*
 * A cap of UINT32_MAX.
 *
 * The jitter bound is the ceiling plus one, which is 2^32 there: the
 * reference's rejection threshold is zero and the draw is one raw 32-bit
 * word. In 32 bits the bound would wrap to zero — an assert in debug and a
 * division by zero in release. Values pinned from index.ts with Rng(7).
 */
static void test_jitter_bound_at_uint32_max_cap(void)
{
    pb_retry_equal_jitter_params eq_max = { 4294967295u, 4294967295u, 8u };
    pb_retry_exponential_full_jitter_params full_max = { 4294967295u, 4294967295u, 8u };
    pb_retry_retry_after_aware_params aware_max = { 4294967295u, 4294967295u, 8u };
    pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
    pb_rng rng_eq;
    pb_rng rng_full;
    pb_rng rng_aware;
    pb_retry *eq;
    pb_retry *full;
    pb_retry *aware;

    pb_rng_init(&rng_eq, 7u);
    pb_rng_init(&rng_full, 7u);
    pb_rng_init(&rng_aware, 7u);
    eq = pb_retry_equal_jitter.create(&eq_max, NULL, &rng_eq);
    full = pb_retry_exponential_full_jitter.create(&full_max, NULL, &rng_full);
    aware = pb_retry_retry_after_aware.create(&aware_max, NULL, &rng_aware);
    PB_CHECK(eq != NULL && full != NULL && aware != NULL);
    if (eq == NULL || full == NULL || aware == NULL) {
        return;
    }

    /* Equal jitter halves first, so its bound of 2^31 never wraps; it pins
     * the shared ceiling at the same inputs the raw-word policies use. */
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(eq, 1u, &error), 2357978250);
    PB_CHECK_I64(pb_retry_equal_jitter.next_delay(eq, 2u, &error), 2858568539);

    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(full, 1u, &error), 210494603);
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(full, 2u, &error), 711084892);
    PB_CHECK_I64(pb_retry_exponential_full_jitter.next_delay(full, 3u, &error), 3993689120);

    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(aware, 1u, &error), 210494603);
    PB_CHECK_I64(pb_retry_retry_after_aware.next_delay(aware, 2u, &error), 711084892);

    pb_retry_equal_jitter.destroy(eq);
    pb_retry_exponential_full_jitter.destroy(full);
    pb_retry_retry_after_aware.destroy(aware);
}

int main(void)
{
    test_null_rng_is_accepted();
    test_seeding_is_deterministic();
    test_no_allocation_after_create();
    test_rejects_bad_params();
    test_decorrelated_walk_is_per_policy();
    test_retry_after_is_honoured_and_clamped();
    test_backoff_ceiling_survives_32_bit_doubling();
    test_jitter_bound_at_uint32_max_cap();
    return pb_test_summary("retry C behaviour");
}

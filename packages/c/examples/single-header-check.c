/*
 * Does the amalgamated header actually work, on its own?
 *
 * The claim `dist/policybook.h` makes is that you can drop one file into a
 * project with no build system, no include path and no library to link, and
 * have every policy in the registry available. This checks it the only way that
 * means anything: by doing exactly that.
 *
 *     cc -std=c99 -Wall -Wextra -Werror -DPOLICYBOOK_IMPLEMENTATION \
 *        -I../dist single-header-check.c -lm -o shc && ./shc
 *
 * Note what is *not* here: no `-I../include`, no `-lpolicybook`. If this
 * compiles, the header is genuinely self-contained. The `-lm` is for libm's
 * `sqrt`, which the Zipf sampler uses — the C standard library, not ours.
 *
 * One policy from each of the four domains, because a header that flattened
 * three of them correctly and dropped the fourth would still link.
 */

/* Guarded, so the file works whether or not the build also passes -D. Without
 * the guard, `-DPOLICYBOOK_IMPLEMENTATION` on the command line makes this a
 * redefinition, which -Werror turns into a failure that says nothing about the
 * header. */
#ifndef POLICYBOOK_IMPLEMENTATION
#define POLICYBOOK_IMPLEMENTATION
#endif
#include "policybook.h"

#include <stdio.h>

int main(void)
{
    int failures = 0;

    /* cache */
    {
        pb_cache_sieve_params params = PB_CACHE_SIEVE_PARAMS_DEFAULT;
        pb_cache *cache;
        params.capacity = 4u;
        cache = pb_cache_sieve.create(&params, NULL, NULL);
        if (cache == NULL) {
            printf("FAIL cache/sieve did not create\n");
            failures += 1;
        } else {
            uint64_t victim;
            unsigned i;
            /* Five keys into a cache of four: the fifth forces a decision. */
            for (i = 0; i < 5u; ++i) {
                pb_cache_sieve.on_access(cache, (uint64_t)i, false, NULL);
                if (i >= 4u) {
                    victim = pb_cache_sieve.evict(cache);
                    if (victim > 4u) {
                        printf("FAIL cache/sieve evicted %lu\n", (unsigned long)victim);
                        failures += 1;
                    }
                }
            }
            printf("ok   cache/sieve\n");
            pb_cache_sieve.destroy(cache);
        }
    }

    /* rate-limiter */
    {
        pb_ratelimiter_token_bucket_params params =
            PB_RATELIMITER_TOKEN_BUCKET_PARAMS_DEFAULT;
        pb_ratelimiter *limiter;
        params.rate_per_sec = 10u;
        params.burst = 1u;
        params.max_keys = 8u;
        limiter = pb_ratelimiter_token_bucket.create(&params, NULL, NULL);
        if (limiter == NULL) {
            printf("FAIL rate-limiter/token-bucket did not create\n");
            failures += 1;
        } else {
            /* One permit, so the first goes through and the second does not. */
            if (!pb_ratelimiter_token_bucket.allow(limiter, 1ull, 1u, 0ull)) {
                printf("FAIL rate-limiter/token-bucket refused the first request\n");
                failures += 1;
            }
            if (pb_ratelimiter_token_bucket.allow(limiter, 1ull, 1u, 0ull)) {
                printf("FAIL rate-limiter/token-bucket admitted past its burst\n");
                failures += 1;
            }
            printf("ok   rate-limiter/token-bucket\n");
            pb_ratelimiter_token_bucket.destroy(limiter);
        }
    }

    /* retry */
    {
        pb_retry_exponential_full_jitter_params params =
            PB_RETRY_EXPONENTIAL_FULL_JITTER_PARAMS_DEFAULT;
        pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
        pb_rng rng;
        pb_retry *policy;
        pb_rng_init(&rng, 42u);
        params.max_attempts = 3u;
        policy = pb_retry_exponential_full_jitter.create(&params, NULL, &rng);
        if (policy == NULL) {
            printf("FAIL retry/exponential-full-jitter did not create\n");
            failures += 1;
        } else {
            if (pb_retry_exponential_full_jitter.next_delay(policy, 3u, &error) !=
                PB_RETRY_GIVE_UP) {
                printf("FAIL retry/exponential-full-jitter did not give up at its budget\n");
                failures += 1;
            }
            printf("ok   retry/exponential-full-jitter\n");
            pb_retry_exponential_full_jitter.destroy(policy);
        }
    }

    /* kv-cache */
    {
        pb_kvcache_streaming_llm_params params = PB_KVCACHE_STREAMING_LLM_PARAMS_DEFAULT;
        pb_kvcache *policy;
        params.budget = 4u;
        params.sinks = 2u;
        policy = pb_kvcache_streaming_llm.create(&params, NULL, NULL);
        if (policy == NULL) {
            printf("FAIL kv-cache/streaming-llm did not create\n");
            failures += 1;
        } else {
            float attn[1] = { 1.0f };
            uint32_t victims[8];
            pb_kvcache_streaming_llm.on_decode_step(policy, 1u, attn, 1u);
            /* Two held against a budget of four: nothing to do yet. */
            if (pb_kvcache_streaming_llm.evict(policy, 4u, victims, 8u) != 0u) {
                printf("FAIL kv-cache/streaming-llm evicted while under budget\n");
                failures += 1;
            }
            printf("ok   kv-cache/streaming-llm\n");
            pb_kvcache_streaming_llm.destroy(policy);
        }
    }

    /* The generators too: a header that flattened the policies but lost the
     * traces would compile and be useless for reproducing a benchmark. */
    {
        const pb_cache_trace_spec *trace = pb_cache_trace_find("zipf-1.0-100k");
        uint32_t keys[16];
        if (trace == NULL || pb_cache_trace_generate(trace, keys, 16u, NULL) != 16u) {
            printf("FAIL the cache trace generator is missing or short\n");
            failures += 1;
        } else {
            printf("ok   trace generators\n");
        }
    }

    if (failures == 0) {
        printf("\nsingle-header-check: the amalgamation stands alone.\n");
        return 0;
    }
    printf("\nsingle-header-check: %d failure(s)\n", failures);
    return 1;
}

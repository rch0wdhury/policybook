/*
 * StreamingLLM over the synthetic attention trace — KV-cache eviction in use.
 *
 * Build (against the installed library):
 *     cc -std=c99 kv_cache_streaming_llm.c -lpolicybook -lm -o kv_cache_streaming_llm
 *
 * A transformer's KV cache grows by one entry per token, per layer, per head.
 * At a long context that is gigabytes, and the value of any individual token is
 * not linear in the sequence — most attention lands on a handful of positions.
 * These policies decide which of the rest to forget.
 *
 * The measurement below is **retained attention mass**: the share of the
 * model's attention that fell on positions the policy still held. It is a proxy
 * for output quality and not a measurement of it — read
 * `policies/kv-cache/README.md` before drawing conclusions from a number like
 * this one.
 */

#include <stdio.h>
#include <stdlib.h>

#include "policybook/kv_cache/kv_cache.h"
#include "policybook/kv_cache/streaming_llm.h"
#include "policybook/kv_cache/traces.h"

#define BUDGET 512u

int main(void)
{
    const pb_kvcache_trace_spec *trace = pb_kvcache_trace_find("decode-4096");
    pb_kvcache_streaming_llm_params params = PB_KVCACHE_STREAMING_LLM_PARAMS_DEFAULT;
    pb_kvcache_trace_gen generator;
    pb_kvcache *policy;
    unsigned char *held;   /* which positions the caller still stores */
    float *visible;        /* attention over those positions, in order */
    uint32_t *victims;
    const float *weights;
    size_t len;
    uint32_t step = 0;
    uint32_t kept = 1u; /* position 0's token exists before decoding starts */
    double retained_total = 0.0;

    if (trace == NULL) {
        fprintf(stderr, "no such trace\n");
        return 1;
    }

    held = (unsigned char *)calloc((size_t)trace->sequence_length, 1u);
    visible = (float *)malloc((size_t)trace->sequence_length * sizeof(float));
    victims = (uint32_t *)malloc((size_t)(BUDGET + 1u) * sizeof(uint32_t));
    if (held == NULL || visible == NULL || victims == NULL) {
        fprintf(stderr, "out of memory\n");
        free(held);
        free(visible);
        free(victims);
        return 1;
    }
    held[0] = 1u;

    if (pb_kvcache_trace_gen_init(&generator, trace, NULL) != 0) {
        fprintf(stderr, "could not start the trace\n");
        free(held);
        free(visible);
        free(victims);
        return 1;
    }

    params.budget = BUDGET;
    params.sinks = 4u; /* the paper's four attention sinks */
    policy = pb_kvcache_streaming_llm.create(&params, NULL, NULL);
    if (policy == NULL) {
        fprintf(stderr, "could not create the policy\n");
        pb_kvcache_trace_gen_destroy(&generator);
        free(held);
        free(visible);
        free(victims);
        return 1;
    }

    while ((weights = pb_kvcache_trace_gen_next(&generator, &len)) != NULL) {
        uint32_t position = (uint32_t)len; /* step t attends over 0 .. t-1 */
        uint32_t visible_count = 0u;
        double retained = 0.0;
        size_t i;

        /* The policy sees attention over what it still holds, in ascending
         * position order, and deliberately *not* renormalised — so the weights
         * sum to less than one by exactly the mass already lost. That gap is
         * the signal the metric below is built on. */
        for (i = 0; i < len; ++i) {
            if (held[i] != 0u) {
                visible[visible_count] = weights[i];
                visible_count += 1u;
                retained += (double)weights[i];
            }
        }
        retained_total += retained;
        step += 1u;

        pb_kvcache_streaming_llm.on_decode_step(policy, position, visible,
                                               (size_t)visible_count);

        held[position] = 1u;
        kept += 1u;

        if (kept > BUDGET) {
            size_t dropped = pb_kvcache_streaming_llm.evict(policy, BUDGET, victims,
                                                            (size_t)(BUDGET + 1u));
            for (i = 0; i < dropped; ++i) {
                held[victims[i]] = 0u;
            }
            kept -= (uint32_t)dropped;
        }
    }

    printf("StreamingLLM on %s at budget %u: %.4f retained attention mass over %lu steps\n",
           trace->id, (unsigned)BUDGET, retained_total / (double)step, (unsigned long)step);
    printf("  %lu bytes of policy state, and nothing allocated after create\n",
           (unsigned long)pb_kvcache_streaming_llm.memory_bytes(policy));

    pb_kvcache_streaming_llm.destroy(policy);
    pb_kvcache_trace_gen_destroy(&generator);
    free(held);
    free(visible);
    free(victims);
    return 0;
}

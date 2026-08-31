/*
 * Prints the memory each rate-limiter policy holds at the reference budget.
 *
 * Not a test — a measuring tool, run by hand when the domain README's memory
 * table needs refreshing. The numbers in that table are measured rather than
 * estimated (the practice established for the cache domain in T10), and this is
 * what measures them.
 *
 *     cmake --build <build> --target measure_ratelimiter_memory
 *     <build>/measure_ratelimiter_memory
 *
 * The reference budget is 100 permits a second, burst 100, over 1,024 keys —
 * each policy's own spelling of it, matching RATE_LIMITER_BENCH_PARAMS in
 * scripts/bench.ts.
 */

#include <stdio.h>

#include "policybook/rate_limiter/dual_bucket.h"
#include "policybook/rate_limiter/fixed_window.h"
#include "policybook/rate_limiter/gcra.h"
#include "policybook/rate_limiter/leaky_bucket.h"
#include "policybook/rate_limiter/rate_limiter.h"
#include "policybook/rate_limiter/sliding_counter.h"
#include "policybook/rate_limiter/sliding_log.h"
#include "policybook/rate_limiter/token_bucket.h"

#define KEYS 1024u

static void report(const char *name, const pb_ratelimiter_vtable *v, const void *params)
{
    pb_ratelimiter *limiter = v->create(params, NULL, NULL);
    size_t bytes;

    if (limiter == NULL) {
        printf("%-16s  (create failed)\n", name);
        return;
    }
    bytes = v->memory_bytes(limiter);
    printf("%-16s  %10lu bytes  %8.1f per key\n", name, (unsigned long)bytes,
           (double)bytes / (double)KEYS);
    v->destroy(limiter);
}

int main(void)
{
    pb_ratelimiter_fixed_window_params fixed_window = { 100u, 1000u, KEYS };
    pb_ratelimiter_sliding_log_params sliding_log = { 100u, 1000u, KEYS };
    pb_ratelimiter_sliding_counter_params sliding_counter = { 100u, 1000u, KEYS };
    pb_ratelimiter_token_bucket_params token_bucket = { 100u, 100u, KEYS };
    pb_ratelimiter_leaky_bucket_params leaky_bucket = { 100u, 100u, KEYS };
    pb_ratelimiter_gcra_params gcra = { 100u, 100u, KEYS };
    pb_ratelimiter_dual_bucket_params dual_bucket = { 6000u, 6000u, KEYS };

    printf("rate-limiter memory at the reference budget, %u keys\n\n", KEYS);
    report("fixed-window", &pb_ratelimiter_fixed_window, &fixed_window);
    report("sliding-log", &pb_ratelimiter_sliding_log, &sliding_log);
    report("sliding-counter", &pb_ratelimiter_sliding_counter, &sliding_counter);
    report("token-bucket", &pb_ratelimiter_token_bucket, &token_bucket);
    report("leaky-bucket", &pb_ratelimiter_leaky_bucket, &leaky_bucket);
    report("gcra", &pb_ratelimiter_gcra, &gcra);
    report("dual-bucket", &pb_ratelimiter_dual_bucket, &dual_bucket);
    return 0;
}

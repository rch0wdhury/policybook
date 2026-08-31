/*
 * GENERATED — do not edit. Regenerate with:
 *     pnpm tsx scripts/assemble-c.ts
 *
 * The umbrella header: everything libpolicybook exports, in one include.
 * Prefer the specific headers in a build you care about the size of.
 */

#ifndef POLICYBOOK_H
#define POLICYBOOK_H

/* Core. */
#include "policybook/allocator.h"
#include "policybook/hash.h"
#include "policybook/rng.h"
#include "policybook/zipf.h"

/* Data structures. */
#include "policybook/ds/heap.h"
#include "policybook/ds/ilist.h"
#include "policybook/ds/map.h"
#include "policybook/ds/ring.h"

/* Domain: cache. */
#include "policybook/cache/cache.h"
#include "policybook/cache/traces.h"
#include "policybook/cache/2q.h"
#include "policybook/cache/arc.h"
#include "policybook/cache/clock.h"
#include "policybook/cache/fifo.h"
#include "policybook/cache/lfu.h"
#include "policybook/cache/lru.h"
#include "policybook/cache/s3_fifo.h"
#include "policybook/cache/sieve.h"
#include "policybook/cache/w_tinylfu.h"

/* Domain: kv-cache. */
#include "policybook/kv_cache/kv_cache.h"
#include "policybook/kv_cache/traces.h"
#include "policybook/kv_cache/h2o.h"
#include "policybook/kv_cache/pyramidkv.h"
#include "policybook/kv_cache/scissorhands.h"
#include "policybook/kv_cache/sliding_window.h"
#include "policybook/kv_cache/snapkv.h"
#include "policybook/kv_cache/streaming_llm.h"
#include "policybook/kv_cache/tova.h"

/* Domain: rate-limiter. */
#include "policybook/rate_limiter/rate_limiter.h"
#include "policybook/rate_limiter/traces.h"
#include "policybook/rate_limiter/dual_bucket.h"
#include "policybook/rate_limiter/fixed_window.h"
#include "policybook/rate_limiter/gcra.h"
#include "policybook/rate_limiter/leaky_bucket.h"
#include "policybook/rate_limiter/sliding_counter.h"
#include "policybook/rate_limiter/sliding_log.h"
#include "policybook/rate_limiter/token_bucket.h"

/* Domain: retry. */
#include "policybook/retry/retry.h"
#include "policybook/retry/traces.h"
#include "policybook/retry/constant.h"
#include "policybook/retry/decorrelated_jitter.h"
#include "policybook/retry/equal_jitter.h"
#include "policybook/retry/exponential.h"
#include "policybook/retry/exponential_full_jitter.h"
#include "policybook/retry/retry_after_aware.h"

#endif /* POLICYBOOK_H */

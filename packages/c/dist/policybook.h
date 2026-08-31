/*
 * policybook — runnable decision policies, as one file.
 *
 * Version 0.1.0. GENERATED — do not edit. Regenerate with:
 *
 *     pnpm tsx scripts/amalgamate-c.ts
 *
 * 46 headers and 40 implementation files, flattened.
 * The originals are under packages/c/ and are what you should read; this is
 * for dropping into a project that would rather not have a build system.
 *
 * Use it like any STB-style header — the declarations come out of every
 * include, the implementation out of exactly one:
 *
 *     #define POLICYBOOK_IMPLEMENTATION
 *     #include "policybook.h"
 *
 *     pb_cache_sieve_params params = PB_CACHE_SIEVE_PARAMS_DEFAULT;
 *     params.capacity = 1024u;
 *     pb_cache *cache = pb_cache_sieve.create(&params, NULL, NULL);
 *
 * Every policy takes all its memory in create and none afterwards, so it is
 * safe on a hot path. Nothing here reads a clock, a file, or an environment
 * variable: time and randomness are always supplied by the caller.
 *
 * Compile the implementation with -ffp-contract=off. Fusing a multiply and
 * an add into one rounding would make a policy's decisions differ from the
 * TypeScript and Python implementations on the same input, which is the one
 * property this registry exists to guarantee.
 *
 * MIT licensed.
 */

#ifndef POLICYBOOK_AMALGAMATED_H
#define POLICYBOOK_AMALGAMATED_H

/* Every system header the library uses, hoisted and de-duplicated. */
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * include/policybook/allocator.h
 * ======================================================================== */

/*
 * pb_allocator — the only way memory is obtained in libpolicybook.
 *
 * Policies are meant to run in places where malloc is unavailable, audited, or
 * simply unwelcome: firmware, kernels, database engines. So every allocation
 * goes through a caller-supplied allocator, and every policy takes all the
 * memory it will ever need in its create function and nothing afterwards
 *.
 *
 * Passing NULL selects malloc/free, which is the right default for a test
 * program and the wrong one for a hot path.
 */

#ifndef POLICYBOOK_ALLOCATOR_H
#define POLICYBOOK_ALLOCATOR_H


#ifdef __cplusplus
extern "C" {
#endif

/*
 * An allocator.
 *
 * `free` receives the original size, so an arena or pool implementation does
 * not have to store a header alongside every block.
 */
typedef struct pb_allocator {
    void *(*alloc)(void *ctx, size_t n);
    void (*free)(void *ctx, void *p, size_t n);
    void *ctx;
} pb_allocator;

/* The malloc/free allocator. Never NULL. */
const pb_allocator *pb_allocator_default(void);

/* Allocate `n` bytes, treating a NULL allocator as the default one. */
static inline void *pb_alloc(const pb_allocator *allocator, size_t n)
{
    const pb_allocator *chosen = (allocator == NULL) ? pb_allocator_default() : allocator;
    return chosen->alloc(chosen->ctx, n);
}

/* Release a block obtained from pb_alloc with the same allocator and size. */
static inline void pb_free(const pb_allocator *allocator, void *p, size_t n)
{
    const pb_allocator *chosen = (allocator == NULL) ? pb_allocator_default() : allocator;
    chosen->free(chosen->ctx, p, n);
}

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_ALLOCATOR_H */

/* ========================================================================
 * include/policybook/rng.h
 * ======================================================================== */

/*
 * pb_rng — the deterministic random number generator shared by every Policybook
 * policy, in every language.
 *
 * This is the C side of the generator defined in `packages/core/src/rng.ts`. It
 * must agree with it bit for bit: same seed, same stream, in TypeScript, Python
 * and C. `tests/gen/test_rng.c` is generated from the shared
 * reference vectors and is the proof.
 *
 * The generator is xoshiro128** (Blackman and Vigna) seeded by splitmix32. It
 * is fast and has a 2^128 - 1 period. It is NOT cryptographically secure.
 *
 * Policies never call rand(); they receive a pb_rng at create time.
 */

#ifndef POLICYBOOK_RNG_H
#define POLICYBOOK_RNG_H


#ifdef __cplusplus
extern "C" {
#endif

/* Generator state. Copyable: copying a pb_rng forks the stream. */
typedef struct pb_rng {
    uint32_t s[4];
} pb_rng;

/*
 * Seed a generator.
 *
 * Every seed, including 0, produces a valid stream.
 */
void pb_rng_init(pb_rng *rng, uint32_t seed);

/* The next 32 random bits. */
uint32_t pb_rng_next_u32(pb_rng *rng);

/*
 * A double in [0, 1) with 32 bits of resolution.
 *
 * Deliberately not the 53-bit variant: one next_u32 divided by 2^32 is exactly
 * the same number in all three languages, with no rounding argument to have.
 */
double pb_rng_next_float(pb_rng *rng);

/*
 * A uniform integer in [0, bound), with no modulo bias.
 *
 * `bound` must be at least 1. Note that the TypeScript and Python versions
 * accept a bound of 2^32, which uint32_t cannot express; C therefore tops out
 * at 2^32 - 1. No vector uses a bound anywhere near that.
 */
uint32_t pb_rng_next_int(pb_rng *rng, uint32_t bound);

/*
 * Hash a key into a well-distributed 32-bit value.
 *
 * The canonical key hash for the registry — sketch indices, virtual-node
 * placement, anything that needs to scatter keys. It is the splitmix32
 * finaliser, the same mix used to expand a seed.
 */
uint32_t pb_mix32(uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RNG_H */

/* ========================================================================
 * include/policybook/cache/cache.h
 * ======================================================================== */

/*
 * The `cache` domain: eviction for a fixed-capacity key cache.
 *
 * A cache holds at most `capacity` keys. When a new key arrives and the cache is
 * full, something has to go, and which one is the whole question. A policy
 * observes every lookup and names a victim when asked.
 *
 * Every policy exports a `const pb_cache_vtable` and a params struct with a
 * _DEFAULT initialiser, so a caller can swap policies at runtime by pointing at
 * a different vtable, or call one policy's functions directly:
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/sieve.h>
 *
 *     pb_cache_sieve_params params = PB_CACHE_SIEVE_PARAMS_DEFAULT;
 *     pb_cache *cache = pb_cache_sieve.create(&params, NULL, NULL);
 *     pb_cache_sieve.on_access(cache, key, false, NULL);
 *     uint64_t victim = pb_cache_sieve.evict(cache);
 *     pb_cache_sieve.destroy(cache);
 *
 * Keys are uint64_t: callers hash their own keys. Time is passed in, never read
 * from a clock. Randomness comes from the pb_rng handed to create, never rand().
 */

#ifndef POLICYBOOK_CACHE_CACHE_H
#define POLICYBOOK_CACHE_CACHE_H



#ifdef __cplusplus
extern "C" {
#endif

/* Opaque per-policy state. */
typedef struct pb_cache pb_cache;

/* Extra information a policy may use, supplied by the caller. */
typedef struct pb_cache_meta {
    uint64_t size; /* entry size, for size-aware policies; 1 if unused */
    uint64_t now;  /* domain time, in arbitrary units; never wall-clock */
} pb_cache_meta;

typedef struct pb_cache_vtable {
    /*
     * Allocate the policy and everything it will ever need.
     *
     * `params` points at the policy's own params struct. `allocator` may be
     * NULL for malloc/free. `rng` may be NULL for a policy that needs no
     * randomness; a policy that does need it must document that.
     *
     * Returns NULL if allocation fails or the params are invalid.
     */
    pb_cache *(*create)(const void *params, const pb_allocator *allocator, pb_rng *rng);

    /* Called on every lookup, before insertion on a miss. `meta` may be NULL. */
    void (*on_access)(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta);

    /* Called when capacity is exceeded. Returns a key the policy currently holds. */
    uint64_t (*evict)(pb_cache *cache);

    /* Optional admission control; NULL if the policy admits everything. */
    bool (*admit)(pb_cache *cache, uint64_t key, const pb_cache_meta *meta);

    /* Release everything create allocated. */
    void (*destroy)(pb_cache *cache);

    /* Bytes held, so a caller can budget. */
    size_t (*memory_bytes)(const pb_cache *cache);

    /*
     * True if the policy allocates after create.
     *
     * No v0.1 policy may set this. It exists so that one which cannot honour
     * the contract has to say so in its vtable rather than quietly breaking a
     * caller's memory budget.
     */
    bool allocates_after_create;

    /* Policy id, e.g. "cache/sieve". */
    const char *id;
} pb_cache_vtable;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_CACHE_H */

/* ========================================================================
 * include/policybook/cache/2q.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/2q/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * 2Q — admit to the main cache only on a second access.
 *
 * New keys audition in A1in, a small FIFO. Keys evicted from it leave their
 * identifier in A1out, a ghost queue holding keys but no values. A key that
 * returns while its ghost is live has proven reuse and enters Am, the main LRU.
 * A scan therefore never reaches Am.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/2q.h>
 *
 *     pb_cache_2q_params params = PB_CACHE_2Q_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_2q.create(&params, NULL, NULL);
 *
 * Memory: about 69 bytes per entry at the default fractions — the resident
 * entries cost roughly what LRU's do, plus about 22 for the A1out ghost ring
 * and its membership table. Call `memory_bytes` for the exact figure.
 *
 * Note that a hit on a key still in A1in does nothing. That is the algorithm:
 * promotion requires a second access *after* eviction, not a repeat reference.
 */

#ifndef POLICYBOOK_CACHE_2Q_H
#define POLICYBOOK_CACHE_2Q_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_2q_params {
    uint32_t capacity; /* maximum number of entries held */
    double kin;        /* fraction of capacity given to A1in */
    double kout;       /* fraction of capacity worth of keys remembered in A1out */
} pb_cache_2q_params;

#define PB_CACHE_2Q_PARAMS_DEFAULT { 1000u, 0.25, 0.5 }

extern const pb_cache_vtable pb_cache_2q;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_2Q_H */

/* ========================================================================
 * include/policybook/cache/arc.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/arc/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * ARC — balance recency against frequency, and tune the balance itself.
 *
 * Four lists: T1 for keys seen once recently, T2 for keys seen at least twice,
 * and ghost lists B1 and B2 holding the identifiers of keys evicted from each.
 * A target p says how much of the cache T1 should get, and every ghost hit
 * moves it: a hit in B1 means recency was undervalued, a hit in B2 means
 * frequency was.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/arc.h>
 *
 *     pb_cache_arc_params params = PB_CACHE_ARC_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_arc.create(&params, NULL, NULL);
 *
 * Memory: about 96 bytes per entry — more than twice LRU's, because ARC tracks
 * up to two keys for every entry it caches. Call `memory_bytes` for the exact
 * figure.
 *
 * There is deliberately no tuning parameter beyond the capacity.
 *
 * See the README's Notes section for the patent history before commercial use.
 */

#ifndef POLICYBOOK_CACHE_ARC_H
#define POLICYBOOK_CACHE_ARC_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_arc_params {
    uint32_t capacity; /* maximum number of entries held */
} pb_cache_arc_params;

#define PB_CACHE_ARC_PARAMS_DEFAULT { 1000u }

extern const pb_cache_vtable pb_cache_arc;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_ARC_H */

/* ========================================================================
 * include/policybook/cache/clock.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/clock/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * CLOCK — approximate LRU with one reference bit per entry.
 *
 * A hit sets a bit and writes nothing shared, so concurrent readers need no
 * lock. Eviction walks a hand around the entries in arrival order, sparing
 * those whose bit is set and clearing it as it passes.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/clock.h>
 *
 *     pb_cache_clock_params params = PB_CACHE_CLOCK_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_clock.create(&params, NULL, NULL);
 *
 * Memory: about 44 bytes per entry — 8 for the key, 1 for the reference bit,
 * 4 for the order ring, 4 for the free stack, and roughly 27 for the hash
 * table. Call `memory_bytes` for the exact figure.
 *
 * Note that a single eviction may walk every entry when all bits are set. The
 * amortised cost is O(1), but the worst-case latency of one call is O(n).
 */

#ifndef POLICYBOOK_CACHE_CLOCK_H
#define POLICYBOOK_CACHE_CLOCK_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_clock_params {
    uint32_t capacity; /* maximum number of entries held */
} pb_cache_clock_params;

#define PB_CACHE_CLOCK_PARAMS_DEFAULT { 1000u }

extern const pb_cache_vtable pb_cache_clock;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_CLOCK_H */

/* ========================================================================
 * include/policybook/cache/fifo.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/fifo/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * FIFO — evict the key that arrived first.
 *
 * The baseline every other cache policy is measured against. It ignores hits
 * entirely, so a hit costs nothing and touches no shared state — the one place
 * FIFO genuinely beats LRU.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/fifo.h>
 *
 *     pb_cache_fifo_params params = PB_CACHE_FIFO_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_fifo.create(&params, NULL, NULL);
 *     pb_cache_fifo.on_access(cache, key, hit, NULL);
 *     uint64_t victim = pb_cache_fifo.evict(cache);
 *     pb_cache_fifo.destroy(cache);
 *
 * Memory: 8 bytes per entry (one key), plus the struct. Nothing else — no
 * links, no counters, no map. The cheapest policy in the domain.
 */

#ifndef POLICYBOOK_CACHE_FIFO_H
#define POLICYBOOK_CACHE_FIFO_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_fifo_params {
    uint32_t capacity; /* maximum number of entries held */
} pb_cache_fifo_params;

#define PB_CACHE_FIFO_PARAMS_DEFAULT { 1000u }

extern const pb_cache_vtable pb_cache_fifo;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_FIFO_H */

/* ========================================================================
 * include/policybook/cache/lfu.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/lfu/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * LFU — evict the key used least often.
 *
 * The O(1) construction of Shah, Mitra and Matani: entries are grouped into
 * frequency classes, the classes form an ascending linked list, and neither
 * promotion nor eviction ever scans.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/lfu.h>
 *
 *     pb_cache_lfu_params params = PB_CACHE_LFU_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_lfu.create(&params, NULL, NULL);
 *
 * Memory: roughly 72 bytes per entry — 8 for the key, 12 for the entry links
 * and class index, 4 for the free stack, about 24 amortised for the hash table,
 * and 24 for the class pool, which is sized to the entry count because there
 * cannot be more distinct frequencies than entries. Call `memory_bytes` for the
 * exact figure.
 *
 * Tie-break: within a frequency class, the entry that reached that frequency
 * earliest is evicted first.
 */

#ifndef POLICYBOOK_CACHE_LFU_H
#define POLICYBOOK_CACHE_LFU_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_lfu_params {
    uint32_t capacity; /* maximum number of entries held */
} pb_cache_lfu_params;

#define PB_CACHE_LFU_PARAMS_DEFAULT { 1000u }

extern const pb_cache_vtable pb_cache_lfu;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_LFU_H */

/* ========================================================================
 * include/policybook/cache/lru.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/lru/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * LRU — evict the key used longest ago.
 *
 * The default baseline. A hash map from key to slot, and a doubly linked list
 * over those slots in recency order.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/lru.h>
 *
 *     pb_cache_lru_params params = PB_CACHE_LRU_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_lru.create(&params, NULL, NULL);
 *
 * Memory: roughly 40 bytes per entry — 8 for the key, 8 for the two list links,
 * and about 24 amortised for the hash table, which is sized at twice the entry
 * count to keep probe chains short. Call `memory_bytes` for the exact figure.
 *
 * Note that every hit writes to the recency list. That is LRU's real
 * operational cost: readers cannot share the cache without synchronisation.
 * CLOCK and SIEVE approximate LRU without writing on the hit path.
 */

#ifndef POLICYBOOK_CACHE_LRU_H
#define POLICYBOOK_CACHE_LRU_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_lru_params {
    uint32_t capacity; /* maximum number of entries held */
} pb_cache_lru_params;

#define PB_CACHE_LRU_PARAMS_DEFAULT { 1000u }

extern const pb_cache_vtable pb_cache_lru;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_LRU_H */

/* ========================================================================
 * include/policybook/cache/s3_fifo.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/s3-fifo/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * S3-FIFO — three FIFO queues, two bits per entry, no list surgery.
 *
 * New keys audition in S, a small FIFO holding a tenth of the cache. An object
 * reused while it sits there is promoted to M, the main FIFO; one that is not
 * falls out and leaves its key in G, a ghost queue with no values behind it. A
 * key returning while its ghost is live skips the audition. Inside M a two-bit
 * counter grants up to three second chances.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/s3_fifo.h>
 *
 *     pb_cache_s3_fifo_params params = PB_CACHE_S3_FIFO_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_s3_fifo.create(&params, NULL, NULL);
 *
 * Memory: about 83 bytes per entry — a key, a two-bit counter and a queue slot
 * per entry, plus the ghost ring and its membership table. Call `memory_bytes`
 * for the exact figure.
 *
 * A hit is a single counter increment: nothing moves, and no other thread needs
 * to observe it. That is what makes this scale where LRU does not.
 */

#ifndef POLICYBOOK_CACHE_S3_FIFO_H
#define POLICYBOOK_CACHE_S3_FIFO_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_s3_fifo_params {
    uint32_t capacity;     /* maximum number of entries held */
    double small_fraction; /* fraction of capacity given to the small queue */
} pb_cache_s3_fifo_params;

#define PB_CACHE_S3_FIFO_PARAMS_DEFAULT { 1000u, 0.1 }

extern const pb_cache_vtable pb_cache_s3_fifo;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_S3_FIFO_H */

/* ========================================================================
 * include/policybook/cache/sieve.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/sieve/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * SIEVE — a FIFO queue with a hand that gives each entry one chance.
 *
 * The same shape as CLOCK, with one difference that changes everything: a
 * survivor is not moved to the back. It stays where it is, and the hand stays
 * where it stopped. Old entries keep having to earn their place; new entries
 * face the hand before travelling the whole queue, so one-hit wonders die young.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/sieve.h>
 *
 *     pb_cache_sieve_params params = PB_CACHE_SIEVE_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_sieve.create(&params, NULL, NULL);
 *
 * Memory: about 48 bytes per entry — 8 for the key, 1 for the visited bit, 8
 * for the two order links, 4 for the free stack, and roughly 27 for the hash
 * table. Call `memory_bytes` for the exact figure.
 *
 * As with CLOCK, a single eviction may sweep every entry when all bits are set:
 * O(1) amortised, O(n) worst case for one call.
 */

#ifndef POLICYBOOK_CACHE_SIEVE_H
#define POLICYBOOK_CACHE_SIEVE_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_sieve_params {
    uint32_t capacity; /* maximum number of entries held */
} pb_cache_sieve_params;

#define PB_CACHE_SIEVE_PARAMS_DEFAULT { 1000u }

extern const pb_cache_vtable pb_cache_sieve;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_SIEVE_H */

/* ========================================================================
 * include/policybook/cache/traces.h
 * ======================================================================== */

/*
 * Canonical traces for the cache domain.
 *
 * The C side of `packages/core/src/domains/cache/traces.ts`, specified in
 * `packages/core/src/domains/cache/TRACES.md`. The generated parity test checks
 * the first 10,000 events of each trace against the committed reference, event
 * for event.
 *
 * Nothing here reads a file, a clock, or an environment variable.
 */

#ifndef POLICYBOOK_CACHE_TRACES_H
#define POLICYBOOK_CACHE_TRACES_H



#ifdef __cplusplus
extern "C" {
#endif

typedef enum pb_cache_trace_kind {
    PB_CACHE_TRACE_ZIPF,
    PB_CACHE_TRACE_SCAN_HEAVY,
    PB_CACHE_TRACE_SHIFTING
} pb_cache_trace_kind;

/* Everything needed to reproduce a trace and to benchmark against it. */
typedef struct pb_cache_trace_spec {
    const char *id;
    pb_cache_trace_kind kind;
    double alpha;          /* Zipf exponent: 1.0 or 0.75 */
    uint32_t keyspace;     /* distinct keys the Zipf body draws from */
    uint32_t key_universe; /* exclusive upper bound on any key emitted */
    uint32_t capacity;     /* cache capacity this trace is benchmarked at */
    uint32_t events;       /* total events the full trace emits */
    uint32_t seed;
} pb_cache_trace_spec;

/* The four canonical cache traces. */
extern const pb_cache_trace_spec pb_cache_traces[];

#define PB_CACHE_TRACE_COUNT 4

/* Look a trace up by id, or NULL if there is no such trace. */
const pb_cache_trace_spec *pb_cache_trace_find(const char *id);

/*
 * Generate a trace into a caller-provided buffer.
 *
 * Writes at most `max_events` keys and returns how many were written. The
 * prefix of a trace is identical to the full trace, so a short run is still
 * reproducible.
 *
 * `allocator` is used for the Zipf table and released before returning; NULL
 * selects malloc/free. Returns 0 if the table could not be allocated.
 */
size_t pb_cache_trace_generate(const pb_cache_trace_spec *spec, uint32_t *out, size_t max_events,
                               const pb_allocator *allocator);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_TRACES_H */

/* ========================================================================
 * include/policybook/cache/w_tinylfu.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/w-tinylfu/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * W-TinyLFU — frequency-based admission on four bits per counter.
 *
 * Approximate counts for far more keys than the cache holds, kept in a
 * fixed-size count-min sketch and halved periodically so the estimate follows
 * the workload. That makes frequency cheap enough to use for admission rather
 * than only eviction: an unpopular newcomer never displaces a proven entry.
 *
 * A small window LRU absorbs new arrivals; when it overflows, its victim's
 * estimated frequency is compared with the main cache's victim, and only the
 * more popular survives. The main cache is a segmented LRU.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/w_tinylfu.h>
 *
 *     pb_cache_w_tinylfu_params params = PB_CACHE_W_TINYLFU_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_w_tinylfu.create(&params, NULL, NULL);
 *
 * Memory: about 65 bytes per entry — the entries themselves cost roughly what
 * LRU's do, plus four bytes of sketch and one bit of doorkeeper per entry. Call
 * `memory_bytes` for the exact figure.
 *
 * Keys are hashed directly by the sketch, so a caller with non-integer keys
 * hashes its own, exactly as the rest of this API expects.
 */

#ifndef POLICYBOOK_CACHE_W_TINYLFU_H
#define POLICYBOOK_CACHE_W_TINYLFU_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_w_tinylfu_params {
    uint32_t capacity;         /* maximum number of entries held */
    double window_fraction;    /* fraction of capacity given to the admission window */
    double protected_fraction; /* fraction of the main cache reserved for protected entries */
} pb_cache_w_tinylfu_params;

#define PB_CACHE_W_TINYLFU_PARAMS_DEFAULT { 1000u, 0.01, 0.8 }

extern const pb_cache_vtable pb_cache_w_tinylfu;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_W_TINYLFU_H */

/* ========================================================================
 * include/policybook/ds/heap.h
 * ======================================================================== */

/*
 * pb_heap — a fixed-capacity 4-ary min-heap of (key, item) pairs.
 *
 * Four children per node rather than two: the tree is shallower, so sift-down
 * does fewer comparisons of cache lines, which is the cost that actually
 * dominates. Used wherever a policy needs "smallest score wins" — Bélády's OPT
 * picking the entry whose next use is furthest away, LFU variants, and so on.
 *
 * Ordering is a total order on (key, item): equal keys are broken by the lower
 * item index. That is not an implementation detail — it is the registry's
 * tie-breaking rule, and it is what makes a heap-driven
 * policy produce the same eviction as the TypeScript and Python versions.
 */

#ifndef POLICYBOOK_DS_HEAP_H
#define POLICYBOOK_DS_HEAP_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_heap {
    uint64_t *keys;
    uint32_t *items;
    uint32_t size;
    uint32_t capacity;
} pb_heap;

/* Allocate a heap holding up to `capacity` entries. The only allocation. */
bool pb_heap_init(pb_heap *heap, uint32_t capacity, const pb_allocator *allocator);

/* Release the heap's arrays. Safe on a zeroed or already-destroyed heap. */
void pb_heap_destroy(pb_heap *heap, const pb_allocator *allocator);

/* Drop every entry, keeping the allocation. */
void pb_heap_clear(pb_heap *heap);

/* Insert a pair. Returns false if the heap is full. */
bool pb_heap_push(pb_heap *heap, uint64_t key, uint32_t item);

/*
 * Remove the smallest (key, item) pair.
 *
 * `key` and `item` may each be NULL if that half is not wanted. Returns false
 * if the heap is empty.
 */
bool pb_heap_pop_min(pb_heap *heap, uint64_t *key, uint32_t *item);

/* Read the smallest pair without removing it. Returns false if empty. */
bool pb_heap_peek_min(const pb_heap *heap, uint64_t *key, uint32_t *item);

/* Bytes held by the heap, for a policy's memory_bytes. */
size_t pb_heap_memory_bytes(const pb_heap *heap);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_DS_HEAP_H */

/* ========================================================================
 * include/policybook/ds/ilist.h
 * ======================================================================== */

/*
 * pb_ilist — a doubly linked list over a fixed set of slot indices.
 *
 * Cache policies are mostly bookkeeping over a fixed number of entries, and the
 * obvious implementation — malloc'd nodes with pointers — is the wrong one
 * here. It scatters the working set across the heap, makes memory use
 * unpredictable, and allocates on the hot path. This list instead stores two
 * uint32_t arrays indexed by slot, allocated once.
 *
 * A "node" is a slot index in [0, capacity). The caller owns whatever the slot
 * means; the list only orders them.
 *
 *   pb_ilist lru;
 *   pb_ilist_init(&lru, 1024, NULL);
 *   pb_ilist_push_front(&lru, slot);      // most recently used
 *   uint32_t victim = pb_ilist_pop_back(&lru);
 *   pb_ilist_destroy(&lru, NULL);
 */

#ifndef POLICYBOOK_DS_ILIST_H
#define POLICYBOOK_DS_ILIST_H



#ifdef __cplusplus
extern "C" {
#endif

/* End of the list. */
#define PB_ILIST_NIL 0xFFFFFFFFu
/* A slot that is not currently in the list. */
#define PB_ILIST_UNLINKED 0xFFFFFFFEu

/* The largest capacity a list can hold, given the two sentinels above. */
#define PB_ILIST_MAX_CAPACITY 0xFFFFFFFEu

typedef struct pb_ilist {
    uint32_t *next;
    uint32_t *prev;
    uint32_t head;
    uint32_t tail;
    uint32_t capacity;
    uint32_t length;
} pb_ilist;

/*
 * Allocate a list holding slots [0, capacity).
 *
 * Returns false if allocation fails or capacity is out of range. This is the
 * only function here that allocates.
 */
bool pb_ilist_init(pb_ilist *list, uint32_t capacity, const pb_allocator *allocator);

/* Release the list's arrays. Safe on a zeroed or already-destroyed list. */
void pb_ilist_destroy(pb_ilist *list, const pb_allocator *allocator);

/* Remove every node, keeping the allocation. */
void pb_ilist_clear(pb_ilist *list);

/* True if `node` is currently linked. */
bool pb_ilist_contains(const pb_ilist *list, uint32_t node);

/* Link `node` at the head. The node must not already be linked. */
void pb_ilist_push_front(pb_ilist *list, uint32_t node);

/* Link `node` at the tail. The node must not already be linked. */
void pb_ilist_push_back(pb_ilist *list, uint32_t node);

/* Unlink `node`. The node must currently be linked. */
void pb_ilist_remove(pb_ilist *list, uint32_t node);

/* Move a linked node to the head. The common operation for LRU on a hit. */
void pb_ilist_move_to_front(pb_ilist *list, uint32_t node);

/* Move a linked node to the tail. */
void pb_ilist_move_to_back(pb_ilist *list, uint32_t node);

/* Unlink and return the head, or PB_ILIST_NIL if the list is empty. */
uint32_t pb_ilist_pop_front(pb_ilist *list);

/* Unlink and return the tail, or PB_ILIST_NIL if the list is empty. */
uint32_t pb_ilist_pop_back(pb_ilist *list);

/* Bytes held by the list, for a policy's memory_bytes. */
size_t pb_ilist_memory_bytes(const pb_ilist *list);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_DS_ILIST_H */

/* ========================================================================
 * include/policybook/ds/map.h
 * ======================================================================== */

/*
 * pb_map — a fixed-capacity hash map from uint64 key to uint32 slot.
 *
 * Cache policies need to answer "where is this key?" on every access, and in C
 * that means bringing your own hash map. This one is built for the same
 * constraints as everything else here: it takes all its memory in init, never
 * grows, never rehashes, and never allocates again.
 *
 * Open addressing with linear probing, and **backward-shift deletion** rather
 * than tombstones. Tombstones would degrade a long-running cache — every
 * eviction leaves one behind, probe chains lengthen without bound, and the
 * usual remedy is a rehash, which allocates. Shifting the following entries
 * back into the hole keeps the table exactly as clean as a fresh one, which is
 * what a cache that evicts millions of times needs.
 *
 * Iteration order is never exposed, deliberately: no policy decision may depend
 * on it.
 */

#ifndef POLICYBOOK_DS_MAP_H
#define POLICYBOOK_DS_MAP_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_map {
    uint64_t *keys;
    uint32_t *values;
    uint8_t *occupied;
    uint32_t mask;     /* capacity - 1; capacity is a power of two */
    uint32_t capacity;
    uint32_t count;
} pb_map;

/*
 * Allocate a table that can hold at least `min_entries` without exceeding a
 * 50% load factor, so probe chains stay short.
 *
 * Returns false on a failed allocation, a zero capacity, or a `min_entries`
 * above 2^30, which no uint32-indexed table can hold at that load.
 */
bool pb_map_init(pb_map *map, uint32_t min_entries, const pb_allocator *allocator);

/* Release the table. Safe on a zeroed or already-destroyed map. */
void pb_map_destroy(pb_map *map, const pb_allocator *allocator);

/* Forget every entry, keeping the allocation. */
void pb_map_clear(pb_map *map);

/*
 * Look a key up.
 *
 * Writes the value through `value` (which may be NULL) and returns true if the
 * key is present.
 */
bool pb_map_get(const pb_map *map, uint64_t key, uint32_t *value);

/*
 * Insert or update.
 *
 * Returns false only if the table is full, which cannot happen when it was
 * sized for the number of entries the caller actually holds.
 */
bool pb_map_put(pb_map *map, uint64_t key, uint32_t value);

/* Remove a key. Returns true if it was present. */
bool pb_map_remove(pb_map *map, uint64_t key);

/* Bytes held, for a policy's memory_bytes. */
size_t pb_map_memory_bytes(const pb_map *map);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_DS_MAP_H */

/* ========================================================================
 * include/policybook/ds/ring.h
 * ======================================================================== */

/*
 * pb_ring — a fixed-capacity circular buffer of slot indices.
 *
 * The FIFO half of most cache policies: S3-FIFO's small and main queues, 2Q's
 * A1in, CLOCK's scan order, the sliding-window limiter's timestamps. Push and
 * pop are O(1) with no allocation and no memmove, and the buffer is contiguous,
 * which is what makes a scan over it cheap.
 */

#ifndef POLICYBOOK_DS_RING_H
#define POLICYBOOK_DS_RING_H



#ifdef __cplusplus
extern "C" {
#endif

/* Returned by pop/peek when the ring is empty. */
#define PB_RING_NIL 0xFFFFFFFFu

typedef struct pb_ring {
    uint32_t *slots;
    uint32_t head; /* index into slots of the oldest entry */
    uint32_t length;
    uint32_t capacity;
} pb_ring;

/* Allocate a ring holding up to `capacity` entries. The only allocation. */
bool pb_ring_init(pb_ring *ring, uint32_t capacity, const pb_allocator *allocator);

/* Release the ring's array. Safe on a zeroed or already-destroyed ring. */
void pb_ring_destroy(pb_ring *ring, const pb_allocator *allocator);

/* Drop every entry, keeping the allocation. */
void pb_ring_clear(pb_ring *ring);

/* Append at the back. Returns false if full. */
bool pb_ring_push_back(pb_ring *ring, uint32_t value);

/* Remove and return the oldest entry, or PB_RING_NIL if empty. */
uint32_t pb_ring_pop_front(pb_ring *ring);

/* Read the oldest entry without removing it, or PB_RING_NIL if empty. */
uint32_t pb_ring_peek_front(const pb_ring *ring);

/*
 * Read the entry `offset` places behind the front.
 *
 * offset 0 is the oldest. Returns PB_RING_NIL if offset is past the end.
 */
uint32_t pb_ring_at(const pb_ring *ring, uint32_t offset);

static inline bool pb_ring_is_empty(const pb_ring *ring)
{
    return ring->length == 0u;
}

static inline bool pb_ring_is_full(const pb_ring *ring)
{
    return ring->length == ring->capacity;
}

/* Bytes held by the ring, for a policy's memory_bytes. */
size_t pb_ring_memory_bytes(const pb_ring *ring);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_DS_RING_H */

/* ========================================================================
 * include/policybook/hash.h
 * ======================================================================== */

/*
 * FNV-1a, 64 bit.
 *
 * Vectors use string keys for policies where the key is opaque, but the C API
 * takes uint64_t keys and leaves hashing to the caller. The C vector generator
 * maps those strings through this function and compares against the mapped
 * expectation, so the same vectors.json drives C without a JSON parser
 *.
 *
 * This is a key-mapping convenience, not the registry's hash function: policies
 * that hash keys internally use pb_mix32 (see rng.h).
 */

#ifndef POLICYBOOK_HASH_H
#define POLICYBOOK_HASH_H


#ifdef __cplusplus
extern "C" {
#endif

#define PB_FNV1A64_OFFSET 0xcbf29ce484222325ULL
#define PB_FNV1A64_PRIME 0x100000001b3ULL

/* Hash `len` bytes of `data`. */
static inline uint64_t pb_fnv1a64(const void *data, size_t len)
{
    const unsigned char *bytes = (const unsigned char *)data;
    uint64_t hash = PB_FNV1A64_OFFSET;
    size_t i;

    for (i = 0; i < len; ++i) {
        hash ^= (uint64_t)bytes[i];
        hash *= PB_FNV1A64_PRIME;
    }
    return hash;
}

/* Hash a NUL-terminated string. */
static inline uint64_t pb_fnv1a64_str(const char *text)
{
    uint64_t hash = PB_FNV1A64_OFFSET;

    while (*text != '\0') {
        hash ^= (uint64_t)(unsigned char)*text++;
        hash *= PB_FNV1A64_PRIME;
    }
    return hash;
}

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_HASH_H */

/* ========================================================================
 * include/policybook/kv_cache/kv_cache.h
 * ======================================================================== */

/*
 * The `kv-cache` domain: which tokens to forget during LLM decoding.
 *
 * A transformer's KV cache grows by one entry per token, per layer, per head.
 * At a 4,096-token context that is gigabytes, and the cost is linear in the
 * sequence while the value of any individual token is not — most attention
 * lands on a handful of positions. Dropping the rest is what makes long
 * contexts affordable, and *which* to drop is this domain.
 *
 * This is the domain where the C implementation earns its keep: the policy runs
 * inside an inference server's per-step budget, so it must not allocate, and it
 * sees only attention weights — never the tensors themselves.
 *
 * Every policy exports a `const pb_kvcache_vtable` and a params struct with a
 * _DEFAULT initialiser:
 *
 *     #include <policybook/kv_cache/kv_cache.h>
 *     #include <policybook/kv_cache/streaming_llm.h>
 *
 *     pb_kvcache_streaming_llm_params params =
 *         PB_KVCACHE_STREAMING_LLM_PARAMS_DEFAULT;
 *     params.budget = 512u;
 *
 *     pb_kvcache *policy = pb_kvcache_streaming_llm.create(&params, NULL, NULL);
 *     pb_kvcache_streaming_llm.on_decode_step(policy, pos, attn, attn_len);
 *     if (kept > params.budget) {
 *         size_t dropped = pb_kvcache_streaming_llm.evict(
 *             policy, params.budget, victims, capacity);
 *     }
 *     pb_kvcache_streaming_llm.destroy(policy);
 *
 * **Positions are absolute token indices**, not offsets into the kept set. A
 * policy that renumbered on eviction could say nothing about where a token sits
 * in the sequence, and nearly every policy here cares: attention sinks are the
 * *first* few positions, recency windows are the *last* few.
 */

#ifndef POLICYBOOK_KV_CACHE_KV_CACHE_H
#define POLICYBOOK_KV_CACHE_KV_CACHE_H



#ifdef __cplusplus
extern "C" {
#endif

/* Opaque per-policy state. */
typedef struct pb_kvcache pb_kvcache;

/*
 * The budgets every canonical benchmark runs.
 *
 * A 4,096-token sequence held at 256, 512 and 1,024 — from a sixteenth of the
 * context to a quarter. Below a sixteenth every policy is mostly noise; above a
 * quarter almost nothing is evicted and they all look alike.
 */
#define PB_KVCACHE_BUDGET_SMALL 256u
#define PB_KVCACHE_BUDGET_MEDIUM 512u
#define PB_KVCACHE_BUDGET_LARGE 1024u

typedef struct pb_kvcache_vtable {
    /*
     * Allocate the policy and everything it will ever need.
     *
     * Everything is allocated here and nothing after, which is binding in this
     * domain rather than merely encouraged: a policy that
     * called malloc on the decode path would add an unbounded pause to every
     * token. That means `budget` must be known at create, and the eviction
     * buffer sized from it.
     *
     * `rng` may be NULL for a deterministic policy, which every policy in this
     * domain is so far.
     */
    pb_kvcache *(*create)(const void *params, const pb_allocator *allocator, pb_rng *rng);

    /*
     * Called once per decode step, before any eviction.
     *
     * `pos` is the position of the token being generated. `attn` holds the
     * attention weights the policy's kept positions received, in ascending
     * position order — so `attn[i]` belongs to the i-th position the policy
     * still holds, and the policy is expected to know its own kept order.
     * `attn_len` is how many, which is exactly the number of positions held.
     *
     * **The kept set starts as {0}.** Position 0's token exists before the
     * first decode step, so the first call is `on_decode_step(policy, 1, attn,
     * 1)` and a policy must initialise its bookkeeping with position 0 held.
     *
     * `attn` may be NULL for a policy that does not read attention at all (a
     * sliding window does not), in which case `attn_len` is 0.
     *
     * **The weights are not renormalised** after eviction. They are the model's
     * own attention over the full sequence, restricted to what the policy kept,
     * so they sum to less than one by exactly the mass already discarded.
     */
    void (*on_decode_step)(pb_kvcache *policy, uint32_t pos, const float *attn,
                           size_t attn_len);

    /*
     * Name positions to drop, writing them into `victims`.
     *
     * Returns how many were written. The returned positions must currently be
     * held, and removing them must bring the kept set to `budget` or below.
     * `capacity` is how many `victims` can take; a policy that would exceed it
     * writes nothing and returns 0, which the harness treats as a failure to
     * evict rather than working around.
     */
    size_t (*evict)(pb_kvcache *policy, uint32_t budget, uint32_t *victims, size_t capacity);

    /* Bytes held by the policy, for the memory column. May be NULL. */
    size_t (*memory_bytes)(const pb_kvcache *policy);

    /* Release everything `create` took. Safe on NULL. */
    void (*destroy)(pb_kvcache *policy);
} pb_kvcache_vtable;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_KV_CACHE_KV_CACHE_H */

/* ========================================================================
 * include/policybook/kv_cache/h2o.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/h2o/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * H2O — keep the tokens that have received the most attention so far.
 *
 * The first policy in this domain that reads the attention weights. Attention
 * is not merely sparse but persistently sparse: a small set of positions
 * accumulates most of it across a generation, and which positions those are is
 * fairly stable. Sum each position's attention over time, and evict the
 * smallest sum.
 *
 *     #include <policybook/kv_cache/kv_cache.h>
 *     #include <policybook/kv_cache/h2o.h>
 *
 *     pb_kvcache_h2o_params params = PB_KVCACHE_H2O_PARAMS_DEFAULT;
 *     params.budget = 512u;
 *
 *     pb_kvcache *policy = pb_kvcache_h2o.create(&params, NULL, NULL);
 *     pb_kvcache_h2o.on_decode_step(policy, pos, attn, attn_len);
 *     size_t n = pb_kvcache_h2o.evict(policy, 512u, victims, cap);
 *     pb_kvcache_h2o.destroy(policy);
 *
 * `rng` may be NULL: this policy is entirely deterministic. `attn` may be NULL,
 * which the policy reads as "no information this step" and leaves every score
 * unchanged.
 *
 * `create` returns NULL if `recent_window >= budget`, which would leave the
 * score nothing to choose between.
 *
 * Scores accumulate in `double` while the weights arrive as `float`. That is
 * deliberate and matches the other two implementations: accumulating 4,000
 * float32 additions would lose low-order bits that the comparison then depends
 * on.
 *
 * Memory: the struct plus `(budget + 1)` each of `uint32_t` positions, `double`
 * scores and one byte of eviction scratch — 13 bytes per slot, so about 6.7 KB
 * at the default budget. Nothing is allocated after create.
 */

#ifndef POLICYBOOK_KV_CACHE_H2O_H
#define POLICYBOOK_KV_CACHE_H2O_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_kvcache_h2o_params {
    uint32_t budget;        /* maximum token positions kept */
    uint32_t recent_window; /* newest positions, never evicted on score */
} pb_kvcache_h2o_params;

#define PB_KVCACHE_H2O_PARAMS_DEFAULT { 512u, 32u }

extern const pb_kvcache_vtable pb_kvcache_h2o;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_KV_CACHE_H2O_H */

/* ========================================================================
 * include/policybook/kv_cache/pyramidkv.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/pyramidkv/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * PyramidKV — spend more cache on early layers than late ones.
 *
 * Every other policy in this domain decides which tokens to keep. This one
 * decides how many, and leaves the choosing to a SnapKV-style rule underneath.
 *
 * Attention in the early layers of a transformer is broad and fairly uniform;
 * in later layers it concentrates onto a few positions. A uniform per-layer
 * budget therefore overfeeds the deep layers and starves the shallow ones, so
 * the budget is redistributed as an arithmetic sequence across layers, with the
 * first getting `pyramid_ratio` times the last and the average preserved.
 *
 *     #include <policybook/kv_cache/kv_cache.h>
 *     #include <policybook/kv_cache/pyramidkv.h>
 *
 *     pb_kvcache_pyramidkv_params params = PB_KVCACHE_PYRAMIDKV_PARAMS_DEFAULT;
 *     params.budget = 512u;
 *     params.num_layers = 32u;
 *     params.layer = 7u;
 *
 *     pb_kvcache *policy = pb_kvcache_pyramidkv.create(&params, NULL, NULL);
 *     pb_kvcache_pyramidkv.on_decode_step(policy, pos, attn, attn_len);
 *     size_t n = pb_kvcache_pyramidkv.evict(policy, 512u, victims, cap);
 *     pb_kvcache_pyramidkv.destroy(policy);
 *
 * One instance serves one layer. A real deployment builds `num_layers` of them
 * and gives each its own `layer`; `pb_kvcache_pyramid_budget` computes a
 * layer's share without constructing anything, for callers sizing buffers up
 * front.
 *
 * With `num_layers == 1` the allocation returns `budget` unchanged and this is
 * exactly SnapKV.
 *
 * `rng` may be NULL: this policy is entirely deterministic. `attn` may be NULL,
 * in which case the call is inert, ring included.
 *
 * `create` returns NULL if `layer >= num_layers`, if `pyramid_ratio` is zero,
 * if `recent_window >= budget`, if `obs_window` is zero, or if `pool_kernel` is
 * even or zero.
 *
 * Memory: as snapkv, sized from whichever of `budget` and this layer's share is
 * larger — a shallow layer's share exceeds the average by design.
 */

#ifndef POLICYBOOK_KV_CACHE_PYRAMIDKV_H
#define POLICYBOOK_KV_CACHE_PYRAMIDKV_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_kvcache_pyramidkv_params {
    uint32_t budget;        /* average positions kept per layer */
    uint32_t layer;         /* which layer this instance serves */
    uint32_t num_layers;    /* layers the budget is shared across */
    uint32_t pyramid_ratio; /* first layer's share over the last layer's */
    uint32_t recent_window; /* newest positions, never evicted on score */
    uint32_t obs_window;    /* decode steps the score is summed over */
    uint32_t pool_kernel;   /* max-pool width across neighbours; must be odd */
} pb_kvcache_pyramidkv_params;

#define PB_KVCACHE_PYRAMIDKV_PARAMS_DEFAULT { 512u, 0u, 1u, 4u, 32u, 16u, 7u }

/*
 * How much cache `layer` gets when `num_layers` share `budget` on average.
 *
 * An arithmetic sequence from 2*budget*r/(r+1) down to 2*budget/(r+1), whose
 * mean is `budget` by construction, evaluated as a single integer division so
 * that all three implementations floor at the same point:
 *
 *     effective(k) = 2*budget*( r*(L-1) - k*(r-1) ) / ( (r+1)*(L-1) )
 *
 * Computed in uint64_t: the numerator reaches 2*budget*ratio*num_layers, which
 * overflows 32 bits for a large model at a large budget.
 *
 * Returns `budget` when `num_layers` is 1 — there is nothing to redistribute,
 * and the denominator would be zero.
 */
uint32_t pb_kvcache_pyramid_budget(uint32_t budget, uint32_t layer, uint32_t num_layers,
                                   uint32_t pyramid_ratio);

extern const pb_kvcache_vtable pb_kvcache_pyramidkv;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_KV_CACHE_PYRAMIDKV_H */

/* ========================================================================
 * include/policybook/kv_cache/scissorhands.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/scissorhands/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * Scissorhands — count how many steps a token mattered for, not how much.
 *
 * The persistence of importance hypothesis: a token influential at one decoding
 * step tends to stay influential. So a position earns a vote on every step where
 * its attention exceeds its fair share — 1 / kept — and the fewest votes is
 * what gets evicted.
 *
 *     #include <policybook/kv_cache/kv_cache.h>
 *     #include <policybook/kv_cache/scissorhands.h>
 *
 *     pb_kvcache_scissorhands_params params =
 *         PB_KVCACHE_SCISSORHANDS_PARAMS_DEFAULT;
 *     params.budget = 512u;
 *
 *     pb_kvcache *policy = pb_kvcache_scissorhands.create(&params, NULL, NULL);
 *     pb_kvcache_scissorhands.on_decode_step(policy, pos, attn, attn_len);
 *     size_t n = pb_kvcache_scissorhands.evict(policy, 512u, victims, cap);
 *     pb_kvcache_scissorhands.destroy(policy);
 *
 * `rng` may be NULL: this policy is entirely deterministic. `attn` may be NULL,
 * which the policy reads as "no information this step" and leaves every vote
 * count unchanged.
 *
 * `create` returns NULL if `recent_window >= budget`, which would leave the
 * votes nothing to choose between.
 *
 * The share comparison is `attn[i] > 1.0 / attn_len` in `double`, strictly —
 * a position that exactly matches its share does not vote. That strictness is
 * pinned by a vector, because "exceeds" and "at least" are exactly the kind of
 * difference that would otherwise diverge quietly between ports.
 *
 * Memory: the struct plus `(budget + 1)` each of `uint32_t` positions,
 * `uint32_t` votes and one byte of eviction scratch — 9 bytes per slot, so
 * about 4.6 KB at the default budget, and rather less than h2o's float64
 * scores. Nothing is allocated after create.
 */

#ifndef POLICYBOOK_KV_CACHE_SCISSORHANDS_H
#define POLICYBOOK_KV_CACHE_SCISSORHANDS_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_kvcache_scissorhands_params {
    uint32_t budget;        /* maximum token positions kept */
    uint32_t recent_window; /* newest positions, never evicted on votes */
} pb_kvcache_scissorhands_params;

#define PB_KVCACHE_SCISSORHANDS_PARAMS_DEFAULT { 512u, 32u }

extern const pb_kvcache_vtable pb_kvcache_scissorhands;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_KV_CACHE_SCISSORHANDS_H */

/* ========================================================================
 * include/policybook/kv_cache/sliding_window.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/sliding-window/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * Sliding window — keep the most recent tokens and forget the rest.
 *
 * The baseline every other policy in this domain is measured against, and a
 * much stronger one than it looks: attention is dominated by recency. What it
 * gets wrong is everything old and still important — above all the attention
 * sinks at the start of the sequence, which is the whole of streaming_llm.
 *
 *     #include <policybook/kv_cache/kv_cache.h>
 *     #include <policybook/kv_cache/sliding_window.h>
 *
 *     pb_kvcache_sliding_window_params params =
 *         PB_KVCACHE_SLIDING_WINDOW_PARAMS_DEFAULT;
 *     params.budget = 512u;
 *
 *     pb_kvcache *policy = pb_kvcache_sliding_window.create(&params, NULL, NULL);
 *     pb_kvcache_sliding_window.on_decode_step(policy, pos, NULL, 0);
 *     size_t n = pb_kvcache_sliding_window.evict(policy, 512u, victims, cap);
 *     pb_kvcache_sliding_window.destroy(policy);
 *
 * `rng` may be NULL: this policy is entirely deterministic. `attn` may be NULL
 * too, and is ignored in any case.
 *
 * Memory: the struct plus `(budget + 1) * sizeof(uint32_t)` for the ring — one
 * slot more than the budget, because that is the most that can be held before
 * an eviction is asked for. Nothing is allocated after create.
 */

#ifndef POLICYBOOK_KV_CACHE_SLIDING_WINDOW_H
#define POLICYBOOK_KV_CACHE_SLIDING_WINDOW_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_kvcache_sliding_window_params {
    uint32_t budget; /* maximum token positions kept */
} pb_kvcache_sliding_window_params;

#define PB_KVCACHE_SLIDING_WINDOW_PARAMS_DEFAULT { 512u }

extern const pb_kvcache_vtable pb_kvcache_sliding_window;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_KV_CACHE_SLIDING_WINDOW_H */

/* ========================================================================
 * include/policybook/kv_cache/snapkv.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/snapkv/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * SnapKV — score on the last few steps, then max-pool across neighbours.
 *
 * Two ideas. A forgetting window: the score is the attention a position
 * received over the last obs_window steps only. And a max-pool across
 * positions: before choosing victims, each score is replaced by the maximum
 * over its pool_kernel neighbours in kept order, so a high scorer defends the
 * tokens around it rather than leaving a fragment of a phrase behind.
 *
 *     #include <policybook/kv_cache/kv_cache.h>
 *     #include <policybook/kv_cache/snapkv.h>
 *
 *     pb_kvcache_snapkv_params params = PB_KVCACHE_SNAPKV_PARAMS_DEFAULT;
 *     params.budget = 512u;
 *
 *     pb_kvcache *policy = pb_kvcache_snapkv.create(&params, NULL, NULL);
 *     pb_kvcache_snapkv.on_decode_step(policy, pos, attn, attn_len);
 *     size_t n = pb_kvcache_snapkv.evict(policy, 512u, victims, cap);
 *     pb_kvcache_snapkv.destroy(policy);
 *
 * `rng` may be NULL: this policy is entirely deterministic. `attn` may be NULL,
 * in which case the call is entirely inert: nothing is recorded and the ring
 * does not advance, so the window spans the last obs_window *observed* steps.
 * Advancing without writing would leave a stale weight in the slot for another
 * full cycle.
 *
 * `create` returns NULL if `recent_window >= budget`, if `obs_window` is zero,
 * or if `pool_kernel` is even or zero. An even kernel has no centre, so the
 * neighbourhood would be lopsided and the pooling would drift one way.
 *
 * Memory: the struct plus, per slot, 4 bytes of position, `obs_window` floats
 * of history, two doubles and a byte of eviction scratch — 4 + 4*obs_window +
 * 17 bytes, so about 45 KB at the defaults. This is much the heaviest policy in
 * the domain, and the history is what costs it. Nothing is allocated after
 * create.
 */

#ifndef POLICYBOOK_KV_CACHE_SNAPKV_H
#define POLICYBOOK_KV_CACHE_SNAPKV_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_kvcache_snapkv_params {
    uint32_t budget;        /* maximum token positions kept */
    uint32_t recent_window; /* newest positions, never evicted on score */
    uint32_t obs_window;    /* decode steps the score is summed over */
    uint32_t pool_kernel;   /* max-pool width across neighbours; must be odd */
} pb_kvcache_snapkv_params;

#define PB_KVCACHE_SNAPKV_PARAMS_DEFAULT { 512u, 32u, 16u, 7u }

extern const pb_kvcache_vtable pb_kvcache_snapkv;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_KV_CACHE_SNAPKV_H */

/* ========================================================================
 * include/policybook/kv_cache/streaming_llm.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/streaming-llm/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * StreamingLLM — a sliding window that also pins the first few tokens.
 *
 * A large, roughly content-independent share of every attention distribution
 * lands on the first few tokens of a sequence. Xiao et al. called them
 * attention sinks; evicting them degrades generation immediately rather than
 * gradually, which is exactly what a plain sliding window does since they are
 * the oldest thing it holds.
 *
 *     #include <policybook/kv_cache/kv_cache.h>
 *     #include <policybook/kv_cache/streaming_llm.h>
 *
 *     pb_kvcache_streaming_llm_params params =
 *         PB_KVCACHE_STREAMING_LLM_PARAMS_DEFAULT;
 *     params.budget = 512u;
 *
 *     pb_kvcache *policy = pb_kvcache_streaming_llm.create(&params, NULL, NULL);
 *     pb_kvcache_streaming_llm.on_decode_step(policy, pos, NULL, 0);
 *     size_t n = pb_kvcache_streaming_llm.evict(policy, 512u, victims, cap);
 *     pb_kvcache_streaming_llm.destroy(policy);
 *
 * `rng` may be NULL: this policy is entirely deterministic. `attn` may be NULL
 * too, and is ignored in any case — it pins the structurally special positions,
 * not the important ones.
 *
 * `create` returns NULL if `sinks >= budget`, which would leave no room for a
 * recency window.
 *
 * Memory: the struct plus `(budget - sinks + 1) * sizeof(uint32_t)` for the
 * window ring. The sinks themselves cost nothing to track — they are the
 * positions below `sinks`, so a count suffices. Nothing is allocated after
 * create.
 */

#ifndef POLICYBOOK_KV_CACHE_STREAMING_LLM_H
#define POLICYBOOK_KV_CACHE_STREAMING_LLM_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_kvcache_streaming_llm_params {
    uint32_t budget; /* maximum token positions kept */
    uint32_t sinks;  /* how many of the first positions are pinned */
} pb_kvcache_streaming_llm_params;

#define PB_KVCACHE_STREAMING_LLM_PARAMS_DEFAULT { 512u, 4u }

extern const pb_kvcache_vtable pb_kvcache_streaming_llm;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_KV_CACHE_STREAMING_LLM_H */

/* ========================================================================
 * include/policybook/kv_cache/tova.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/tova/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * TOVA — drop whichever token the model just stopped looking at.
 *
 * A decoder-only transformer is really a multi-state RNN whose state is the KV
 * cache, and at each step the model tells you through its attention which
 * states it is using. Drop the least-used one. There is no accumulation, so
 * nothing a position did earlier defends it.
 *
 *     #include <policybook/kv_cache/kv_cache.h>
 *     #include <policybook/kv_cache/tova.h>
 *
 *     pb_kvcache_tova_params params = PB_KVCACHE_TOVA_PARAMS_DEFAULT;
 *     params.budget = 512u;
 *
 *     pb_kvcache *policy = pb_kvcache_tova.create(&params, NULL, NULL);
 *     pb_kvcache_tova.on_decode_step(policy, pos, attn, attn_len);
 *     size_t n = pb_kvcache_tova.evict(policy, 512u, victims, cap);
 *     pb_kvcache_tova.destroy(policy);
 *
 * `rng` may be NULL: this policy is entirely deterministic. `attn` may be NULL,
 * in which case no position's record is updated.
 *
 * There is no recent-window parameter, unlike every other scoring policy here.
 * Recency is already in the signal — recent tokens attract high attention now.
 * The token just generated is admitted as *unobserved* and is not a candidate
 * for eviction until something has attended to it, which is a refusal to rank
 * it on evidence that does not exist rather than a recency rule.
 *
 * Memory: the struct plus `(budget + 1)` each of `uint32_t` positions, `double`
 * last-attention and one byte of eviction scratch — 13 bytes per slot, so about
 * 6.7 KB at the default budget. Nothing is allocated after create.
 */

#ifndef POLICYBOOK_KV_CACHE_TOVA_H
#define POLICYBOOK_KV_CACHE_TOVA_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_kvcache_tova_params {
    uint32_t budget; /* maximum token positions kept */
} pb_kvcache_tova_params;

#define PB_KVCACHE_TOVA_PARAMS_DEFAULT { 512u }

extern const pb_kvcache_vtable pb_kvcache_tova;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_KV_CACHE_TOVA_H */

/* ========================================================================
 * include/policybook/kv_cache/traces.h
 * ======================================================================== */

/*
 * The synthetic attention generator.
 *
 * The C side of `packages/core/src/domains/kv-cache/traces.ts`, specified in
 * `packages/core/src/domains/kv-cache/TRACES.md`. The generated parity test
 * checks the float32 bit patterns of the first ten steps and a hash of every
 * step against the committed reference.
 *
 * **This is the only domain whose trace is floating-point**, which makes parity
 * a stronger requirement here than anywhere else in the registry. A single
 * differing ULP in one weight propagates through the normalisation into every
 * other weight of that step, so the parity artefact is bit patterns rather than
 * values. That rests on three rules, shared with the TypeScript and Python
 * implementations:
 *
 *   - contributions accumulate in a pinned order — sink, local, heavy, noise —
 *     because float addition is not associative;
 *   - everything is `double` until the very end, where one division per
 *     position normalises and one cast to `float` rounds;
 *   - no transcendental function appears anywhere. Weights are `65 - d` and
 *     `1 / (rank + 1)`, because `pow` and `exp` are not correctly rounded
 *     across C standard libraries.
 *
 * The library is compiled with `-ffp-contract=off`, without which a compiler
 * could fuse a multiply and an add into a single rounding and produce weights
 * this generator's own reference does not contain.
 *
 * The generator is a stateful object rather than a function returning an array:
 * the heavy-hitter set carries between steps, and a 4,095-step run would
 * otherwise allocate four thousand buffers. Everything it needs is allocated in
 * `init` and reused, so stepping costs nothing.
 *
 * Nothing here reads a file, a clock, or an environment variable.
 */

#ifndef POLICYBOOK_KV_CACHE_TRACES_H
#define POLICYBOOK_KV_CACHE_TRACES_H



#ifdef __cplusplus
extern "C" {
#endif

/* Everything needed to reproduce an attention trace. */
typedef struct pb_kvcache_trace_spec {
    const char *id;
    uint32_t sequence_length; /* step t attends over positions 0 .. t-1 */
    uint32_t seed;
} pb_kvcache_trace_spec;

/* The canonical kv-cache workload. */
extern const pb_kvcache_trace_spec pb_kvcache_traces[];

#define PB_KVCACHE_TRACE_COUNT 1

/* How many heavy hitters are live at once. */
#define PB_KVCACHE_HEAVY_COUNT 32u

/* Look a trace up by id, or NULL if there is no such trace. */
const pb_kvcache_trace_spec *pb_kvcache_trace_find(const char *id);

/*
 * A trace in progress: the random stream, the live heavy set, and the buffers.
 *
 * Treat the fields as private. The struct is exposed so it can live on the
 * caller's stack, not so it can be poked at.
 */
typedef struct pb_kvcache_trace_gen {
    const pb_kvcache_trace_spec *spec;
    const pb_allocator *allocator;
    pb_rng rng;
    uint32_t step;                              /* the step just produced; 0 before the first */
    uint32_t heavy[PB_KVCACHE_HEAVY_COUNT];     /* live heavy positions, in draw order */
    size_t heavy_len;
    uint8_t *drawn;   /* membership flags for the rejection sampler */
    double *scratch;  /* float64 accumulators, one per position */
    float *weights;   /* the step's float32 weights */
} pb_kvcache_trace_gen;

/*
 * Prepare `gen` to produce `spec`'s steps from the beginning.
 *
 * Allocates three buffers of `spec->sequence_length` entries; returns 0 on
 * success and -1 if allocation failed, in which case `gen` is safe to destroy.
 * `allocator` may be NULL for the default one.
 */
int pb_kvcache_trace_gen_init(pb_kvcache_trace_gen *gen, const pb_kvcache_trace_spec *spec,
                              const pb_allocator *allocator);

/*
 * Produce the next step's weights, or NULL when the trace is exhausted.
 *
 * The returned pointer is `gen`'s own buffer: valid until the next call, and
 * never to be freed by the caller. `*len` receives the number of weights, which
 * at step t is t.
 *
 * The last step is `sequence_length - 1`, not `sequence_length`: the attending
 * token at step t sits at position t, and the final position of an N-token
 * sequence is N-1. Position 0's token exists before decoding starts and never
 * attends.
 */
const float *pb_kvcache_trace_gen_next(pb_kvcache_trace_gen *gen, size_t *len);

/* Release the buffers. Safe on a zeroed or already-destroyed generator. */
void pb_kvcache_trace_gen_destroy(pb_kvcache_trace_gen *gen);

/*
 * FNV-1a 32 over the float32 bit patterns of every step, little-endian.
 *
 * The parity artefact for this domain. Committing whole traces would mean eight
 * million floats; a hash says the same thing in one number, and the committed
 * first steps say *where* a divergence starts.
 *
 * Returns 0 if the generator could not be allocated, which is not a value the
 * real hash can take.
 */
uint32_t pb_kvcache_trace_hash(const pb_kvcache_trace_spec *spec, const pb_allocator *allocator);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_KV_CACHE_TRACES_H */

/* ========================================================================
 * include/policybook/zipf.h
 * ======================================================================== */

/*
 * Zipf sampling for the canonical traces.
 *
 * The C side of `packages/core/src/zipf.ts`. It must produce the same ranks
 * from the same seed, draw for draw.
 *
 * Only two exponents are supported, and that is deliberate. A Zipf weight is
 * 1 / rank^alpha, which wants pow — but pow is not correctly rounded, so the
 * same call can return different doubles on different C standard libraries, and
 * a trace that differs by one ULP eventually samples a different key. The two
 * supported exponents need only sqrt, which IEEE-754 requires to be correctly
 * rounded everywhere:
 *
 *     alpha = 1.00 -> 1 / r
 *     alpha = 0.75 -> 1 / (sqrt(r) * sqrt(sqrt(r)))
 *
 * See packages/core/src/domains/cache/TRACES.md for the specification.
 */

#ifndef POLICYBOOK_ZIPF_H
#define POLICYBOOK_ZIPF_H



#ifdef __cplusplus
extern "C" {
#endif

/* A precomputed Zipf distribution over ranks 0 .. size - 1. */
typedef struct pb_zipf {
    double *cumulative; /* ascending cumulative weights */
    double total;
    uint32_t size;
} pb_zipf;

/*
 * Build the cumulative table.
 *
 * `alpha` must be 1.0 or 0.75; anything else is refused. Returns false on a bad
 * argument or a failed allocation. This is the only function here that
 * allocates — sampling does not.
 */
bool pb_zipf_init(pb_zipf *zipf, uint32_t size, double alpha, const pb_allocator *allocator);

/* Release the table. Safe on a zeroed or already-destroyed sampler. */
void pb_zipf_destroy(pb_zipf *zipf, const pb_allocator *allocator);

/*
 * Draw a rank, consuming exactly one pb_rng_next_float.
 *
 * The count matters: a port that consumed two would diverge from the reference
 * trace on every subsequent event.
 */
uint32_t pb_zipf_sample(const pb_zipf *zipf, pb_rng *rng);

/* The weight of one rank, without calling pow. */
double pb_zipf_weight(uint32_t rank, double alpha);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_ZIPF_H */

/* ========================================================================
 * include/policybook/rate_limiter/rate_limiter.h
 * ======================================================================== */

/*
 * The `rate-limiter` domain: deciding whether a request may proceed.
 *
 * A limiter is asked one question — may this request go through, right now? —
 * and the interesting part is what it does with the ones it refuses. A fixed
 * window lets through twice its limit at a window boundary. A token bucket
 * absorbs bursts by design. A leaky bucket refuses to.
 *
 * Every policy exports a `const pb_ratelimiter_vtable` and a params struct with
 * a _DEFAULT initialiser, so a caller can swap policies at runtime by pointing
 * at a different vtable, or call one policy's functions directly:
 *
 *     #include <policybook/rate_limiter/rate_limiter.h>
 *     #include <policybook/rate_limiter/token_bucket.h>
 *
 *     pb_ratelimiter_token_bucket_params params =
 *         PB_RATELIMITER_TOKEN_BUCKET_PARAMS_DEFAULT;
 *     pb_ratelimiter *limiter = pb_ratelimiter_token_bucket.create(&params, NULL, NULL);
 *     if (pb_ratelimiter_token_bucket.allow(limiter, key, 1u, now_ms)) { ... }
 *     pb_ratelimiter_token_bucket.destroy(limiter);
 *
 * Keys are uint64_t: callers hash their own keys. **Time is an integer number of
 * milliseconds passed in by the caller, never read from a clock** — a policy
 * that reads the clock cannot be tested, and a caller with its own time source
 * could not use it. Every operation on that time is integer arithmetic too:
 * milli-token ledgers with an explicit carry rather than floating-point token
 * counts. Floats drift, and three languages
 * drift differently.
 */

#ifndef POLICYBOOK_RATE_LIMITER_RATE_LIMITER_H
#define POLICYBOOK_RATE_LIMITER_RATE_LIMITER_H



#ifdef __cplusplus
extern "C" {
#endif

/* Opaque per-policy state. */
typedef struct pb_ratelimiter pb_ratelimiter;

/*
 * The reference configuration every canonical benchmark uses.
 *
 * Policies express their limits differently — permits per window, tokens per
 * second, an emission interval — so it is stated once in neutral terms and each
 * policy's README says how it maps onto its own parameters.
 */
#define PB_RATELIMITER_REFERENCE_PERMITS_PER_SECOND 100u
#define PB_RATELIMITER_REFERENCE_BURST 100u

/*
 * Returned by `retry_after` when the policy genuinely cannot say.
 *
 * In practice this means one thing: the key is not tracked and `max_keys` is
 * exhausted, so it will never be admitted and no finite wait is truthful. The
 * fuzzer treats any other use as a violation — a policy that returns this and
 * then admits the very next request was simply wrong.
 *
 * Returning 0 here instead would be a lie a caller acts on, and it is the bug
 * the rate-limiter fuzzer found on its first run.
 */
#define PB_RATELIMITER_RETRY_UNKNOWN UINT64_MAX

typedef struct pb_ratelimiter_vtable {
    /*
     * Allocate the policy and everything it will ever need.
     *
     * `params` points at the policy's own params struct. `allocator` may be
     * NULL for malloc/free. `rng` may be NULL for a policy that needs no
     * randomness.
     *
     * Returns NULL if allocation fails or the params are invalid.
     */
    pb_ratelimiter *(*create)(const void *params, const pb_allocator *allocator, pb_rng *rng);

    /*
     * May a request of `cost` units for `key` proceed at `now_ms`?
     *
     * `now_ms` is non-decreasing. Returning true is the decision *and* the
     * commitment: whatever budget the request consumes has been consumed by the
     * time this returns, so a policy must not be called speculatively.
     */
    bool (*allow)(pb_ratelimiter *limiter, uint64_t key, uint32_t cost, uint64_t now_ms);

    /*
     * How long until `key` could succeed, in milliseconds. A hint, not a
     * promise: it reflects only what the policy can prove from its own state at
     * `now_ms`, so a caller that waits exactly this long may still be refused
     * if others arrive meanwhile. Zero means "try now".
     *
     * May be NULL for a policy that does not implement it.
     */
    uint64_t (*retry_after)(pb_ratelimiter *limiter, uint64_t key, uint64_t now_ms);

    /*
     * How many keys the policy is currently tracking.
     *
     * Optional introspection for the memory metric: the number that separates a limiter you can run for a million keys
     * from one you cannot. May be NULL.
     */
    size_t (*state_size)(const pb_ratelimiter *limiter);

    /* Bytes held by the policy, for the memory column. May be NULL. */
    size_t (*memory_bytes)(const pb_ratelimiter *limiter);

    /* Release everything `create` took. Safe on NULL. */
    void (*destroy)(pb_ratelimiter *limiter);
} pb_ratelimiter_vtable;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_RATE_LIMITER_H */

/* ========================================================================
 * include/policybook/rate_limiter/traces.h
 * ======================================================================== */

/*
 * Canonical traces for the rate-limiter domain.
 *
 * The C side of `packages/core/src/domains/rate-limiter/traces.ts`, specified
 * in `packages/core/src/domains/rate-limiter/TRACES.md`. The generated parity
 * test checks the first 10,000 events of each trace against the committed
 * reference, event for event.
 *
 * Arrivals are a per-millisecond Bernoulli process rather than a Poisson one
 *: a Poisson inter-arrival time needs `log`,
 * which is not correctly rounded across C standard libraries, so the three
 * ports would eventually disagree.
 *
 * Nothing here reads a file, a clock, or an environment variable.
 */

#ifndef POLICYBOOK_RATE_LIMITER_TRACES_H
#define POLICYBOOK_RATE_LIMITER_TRACES_H



#ifdef __cplusplus
extern "C" {
#endif

typedef enum pb_ratelimiter_trace_kind {
    /* One key, one Bernoulli trial per millisecond. `steady` and `overload`
     * share this generator and differ only in `arrival_p`. */
    PB_RATELIMITER_TRACE_SINGLE_KEY,
    PB_RATELIMITER_TRACE_BURSTY,
    PB_RATELIMITER_TRACE_MANY_KEYS
} pb_ratelimiter_trace_kind;

/* Everything needed to reproduce a trace and to benchmark against it. */
typedef struct pb_ratelimiter_trace_spec {
    const char *id;
    pb_ratelimiter_trace_kind kind;
    uint32_t duration_ms;  /* length of the simulated period */
    uint32_t key_universe; /* exclusive upper bound on any key emitted */
    uint32_t keyspace;     /* distinct keys the Zipf body draws from, or 0 */
    double arrival_p;      /* Bernoulli probability per millisecond */
    uint32_t seed;
} pb_ratelimiter_trace_spec;

/* The three canonical rate-limiter traces. */
extern const pb_ratelimiter_trace_spec pb_ratelimiter_traces[];

#define PB_RATELIMITER_TRACE_COUNT 4

/* Look a trace up by id, or NULL if there is no such trace. */
const pb_ratelimiter_trace_spec *pb_ratelimiter_trace_find(const char *id);

/*
 * Generate a trace into caller-provided buffers.
 *
 * Writes at most `max_events` arrivals into `times` and `keys` — both must have
 * room for `max_events` — and returns how many were written. That count is the
 * outcome of a Bernoulli process, so it is not known in advance and may be
 * fewer than `max_events` even for the full trace: `steady` produces 5,491
 * arrivals and `bursty` 3,020. Generation consumes the random stream in order,
 * so a truncated trace is exactly a prefix of the full one.
 *
 * `allocator` is used for the Zipf table and released before returning; NULL
 * selects malloc/free. Returns 0 if the table could not be allocated.
 */
size_t pb_ratelimiter_trace_generate(const pb_ratelimiter_trace_spec *spec, uint32_t *times,
                                     uint32_t *keys, size_t max_events,
                                     const pb_allocator *allocator);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_TRACES_H */

/* ========================================================================
 * include/policybook/rate_limiter/dual_bucket.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/dual-bucket/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * DualBucket — two limits at once, and a request must satisfy both.
 *
 * A requests-per-minute ceiling and a tokens-per-minute ceiling, checked
 * together. One dimension counts calls, the other counts how much work each
 * call asks for, and either can refuse on its own. This is the shape every LLM
 * API uses.
 *
 *     #include <policybook/rate_limiter/rate_limiter.h>
 *     #include <policybook/rate_limiter/dual_bucket.h>
 *
 *     pb_ratelimiter_dual_bucket_params params =
 *         PB_RATELIMITER_DUAL_BUCKET_PARAMS_DEFAULT;
 *     pb_ratelimiter *limiter = pb_ratelimiter_dual_bucket.create(&params, NULL, NULL);
 *     // `cost` is the work the call asks for; it always charges one request.
 *     if (pb_ratelimiter_dual_bucket.allow(limiter, key, token_count, now_ms)) { ... }
 *     pb_ratelimiter_dual_bucket.destroy(limiter);
 *
 * Each dimension is a token bucket with a per-minute period. **The charge is
 * atomic**: if either dimension would refuse, neither is charged, so a caller
 * refused for work has not quietly spent a request too.
 *
 * `retry_after` reports the later of the two dimensions and assumes a smallest
 * possible call, since the vtable has no cost argument for it.
 *
 * **`max_keys` is a C-only parameter.** The TypeScript and Python ports grow a
 * hash map without limit; C takes all its memory in `create` and never
 * allocates again. Once the table is full a key that has
 * never been seen is refused — fail-closed.
 *
 * Memory: 24 bytes per tracked key, plus the map.
 */

#ifndef POLICYBOOK_RATE_LIMITER_DUAL_BUCKET_H
#define POLICYBOOK_RATE_LIMITER_DUAL_BUCKET_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_ratelimiter_dual_bucket_params {
    uint32_t requests_per_min; /* calls allowed per minute, regardless of size */
    uint32_t tokens_per_min;   /* units of work allowed per minute */
    uint32_t max_keys;         /* C only: keys tracked before new ones are refused */
} pb_ratelimiter_dual_bucket_params;

#define PB_RATELIMITER_DUAL_BUCKET_PARAMS_DEFAULT { 500u, 200000u, 1024u }

extern const pb_ratelimiter_vtable pb_ratelimiter_dual_bucket;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_DUAL_BUCKET_H */

/* ========================================================================
 * include/policybook/rate_limiter/fixed_window.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/fixed-window/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * FixedWindow — count requests inside a clock-aligned window, reset at the edge.
 *
 * The simplest limiter that works. Two integers per key, no allocation on the
 * hot path, and one well-known flaw: a client can send `limit` requests at the
 * end of one window and `limit` more at the start of the next, putting
 * 2 x limit through in an interval shorter than a single window.
 *
 *     #include <policybook/rate_limiter/rate_limiter.h>
 *     #include <policybook/rate_limiter/fixed_window.h>
 *
 *     pb_ratelimiter_fixed_window_params params =
 *         PB_RATELIMITER_FIXED_WINDOW_PARAMS_DEFAULT;
 *     params.limit = 100u;
 *     pb_ratelimiter *limiter = pb_ratelimiter_fixed_window.create(&params, NULL, NULL);
 *     if (pb_ratelimiter_fixed_window.allow(limiter, key, 1u, now_ms)) { ... }
 *     pb_ratelimiter_fixed_window.destroy(limiter);
 *
 * Windows are aligned to the epoch, not to a key's first request, which is what
 * lets separate processes agree on the current window without coordinating.
 *
 * **`max_keys` is a C-only parameter.** The TypeScript and Python ports grow a
 * hash map without limit; C takes all its memory in `create` and never
 * allocates again, so the number of tracked keys has to be
 * bounded up front. Once the table is full, a key that has never been seen is
 * **refused** — fail-closed, because the alternative is to stop limiting the
 * moment an attacker cycles through keys. Size it above the cardinality you
 * expect.
 *
 * Memory: 16 bytes per tracked key (a 64-bit window start and a 32-bit count,
 * padded), plus the map.
 */

#ifndef POLICYBOOK_RATE_LIMITER_FIXED_WINDOW_H
#define POLICYBOOK_RATE_LIMITER_FIXED_WINDOW_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_ratelimiter_fixed_window_params {
    uint32_t limit;     /* requests allowed per window */
    uint32_t window_ms; /* window length, in milliseconds */
    uint32_t max_keys;  /* C only: keys tracked before new ones are refused */
} pb_ratelimiter_fixed_window_params;

#define PB_RATELIMITER_FIXED_WINDOW_PARAMS_DEFAULT { 100u, 1000u, 1024u }

extern const pb_ratelimiter_vtable pb_ratelimiter_fixed_window;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_FIXED_WINDOW_H */

/* ========================================================================
 * include/policybook/rate_limiter/gcra.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/gcra/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * Gcra — the token bucket, kept as one number instead of three.
 *
 * The Generic Cell Rate Algorithm stores a single theoretical arrival time per
 * key: the instant at which the next request would be exactly on schedule. How
 * many permits are banked, and how much of the next one has accrued, are both
 * implied by how far that instant sits from now.
 *
 *     #include <policybook/rate_limiter/rate_limiter.h>
 *     #include <policybook/rate_limiter/gcra.h>
 *
 *     pb_ratelimiter_gcra_params params = PB_RATELIMITER_GCRA_PARAMS_DEFAULT;
 *     pb_ratelimiter *limiter = pb_ratelimiter_gcra.create(&params, NULL, NULL);
 *     if (pb_ratelimiter_gcra.allow(limiter, key, 1u, now_ms)) { ... }
 *     pb_ratelimiter_gcra.destroy(limiter);
 *
 * It admits and refuses exactly what `pb_ratelimiter_token_bucket` does,
 * including the fractional carry, with `retry_after` agreeing to the
 * millisecond. The reason to choose it is state: **one uint64 per key** rather
 * than three fields.
 *
 * The arithmetic is exact integers in units scaled by the rate. The textbook
 * form needs an emission interval of `1000 / rate_per_sec` milliseconds, which
 * is not whole for most rates; multiplying through by the rate clears it, so
 * one permit costs exactly 1,000 units and one millisecond is `rate_per_sec` of
 * them.
 *
 * **`max_keys` is a C-only parameter.** The TypeScript and Python ports grow a
 * hash map without limit; C takes all its memory in `create` and never
 * allocates again. Once the table is full a key that has
 * never been seen is refused — fail-closed.
 *
 * Memory: 8 bytes per tracked key, plus the map.
 */

#ifndef POLICYBOOK_RATE_LIMITER_GCRA_H
#define POLICYBOOK_RATE_LIMITER_GCRA_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_ratelimiter_gcra_params {
    uint32_t rate_per_sec; /* permits per second, sustained */
    uint32_t burst;        /* permits spendable at once after an idle period */
    uint32_t max_keys;     /* C only: keys tracked before new ones are refused */
} pb_ratelimiter_gcra_params;

#define PB_RATELIMITER_GCRA_PARAMS_DEFAULT { 100u, 100u, 1024u }

extern const pb_ratelimiter_vtable pb_ratelimiter_gcra;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_GCRA_H */

/* ========================================================================
 * include/policybook/rate_limiter/leaky_bucket.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/leaky-bucket/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * LeakyBucket — a level that rises with each request and drains at a steady rate.
 *
 * The meter formulation: every admitted request adds one unit to a bucket that
 * leaks at `rate_per_sec`, and a request that would overflow `capacity` is
 * refused. At the default capacity of 1 that means exact spacing — one request
 * every 1000/rate_per_sec milliseconds, never two together.
 *
 *     #include <policybook/rate_limiter/rate_limiter.h>
 *     #include <policybook/rate_limiter/leaky_bucket.h>
 *
 *     pb_ratelimiter_leaky_bucket_params params =
 *         PB_RATELIMITER_LEAKY_BUCKET_PARAMS_DEFAULT;
 *     pb_ratelimiter *limiter = pb_ratelimiter_leaky_bucket.create(&params, NULL, NULL);
 *     if (pb_ratelimiter_leaky_bucket.allow(limiter, key, 1u, now_ms)) { ... }
 *     pb_ratelimiter_leaky_bucket.destroy(limiter);
 *
 * At equal parameters this is `pb_ratelimiter_token_bucket` under the
 * substitution `tokens = capacity - level`. Both ship because the names are
 * what people look for; neither is faster. The default capacity is 1 rather
 * than the domain's reference burst of 100, because a caller who wants a burst
 * allowance is describing a token bucket.
 *
 * **`max_keys` is a C-only parameter.** The TypeScript and Python ports grow a
 * hash map without limit; C takes all its memory in `create` and never
 * allocates again. Once the table is full a key that has
 * never been seen is refused — fail-closed.
 *
 * Memory: 24 bytes per tracked key, plus the map.
 */

#ifndef POLICYBOOK_RATE_LIMITER_LEAKY_BUCKET_H
#define POLICYBOOK_RATE_LIMITER_LEAKY_BUCKET_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_ratelimiter_leaky_bucket_params {
    uint32_t rate_per_sec; /* units drained per second */
    uint32_t capacity;     /* maximum level, and so the largest burst that fits */
    uint32_t max_keys;     /* C only: keys tracked before new ones are refused */
} pb_ratelimiter_leaky_bucket_params;

#define PB_RATELIMITER_LEAKY_BUCKET_PARAMS_DEFAULT { 100u, 1u, 1024u }

extern const pb_ratelimiter_vtable pb_ratelimiter_leaky_bucket;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_LEAKY_BUCKET_H */

/* ========================================================================
 * include/policybook/rate_limiter/sliding_counter.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/sliding-counter/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * SlidingCounter — two fixed windows, weighted by how far into the new one you are.
 *
 * The practical compromise between a fixed window and a sliding log. Keep the
 * count for the current window and the previous one, then fade the old count
 * out as the new window fills:
 *
 *     estimate = previous * (window_ms - elapsed) / window_ms + current
 *
 * Three integers per key instead of a hundred timestamps, and the boundary
 * burst is gone.
 *
 *     #include <policybook/rate_limiter/rate_limiter.h>
 *     #include <policybook/rate_limiter/sliding_counter.h>
 *
 *     pb_ratelimiter_sliding_counter_params params =
 *         PB_RATELIMITER_SLIDING_COUNTER_PARAMS_DEFAULT;
 *     pb_ratelimiter *limiter = pb_ratelimiter_sliding_counter.create(&params, NULL, NULL);
 *     if (pb_ratelimiter_sliding_counter.allow(limiter, key, 1u, now_ms)) { ... }
 *     pb_ratelimiter_sliding_counter.destroy(limiter);
 *
 * The weighting is integer arithmetic with the remainder discarded, matching
 * the TypeScript and Python ports exactly. A
 * floating-point version of this line would eventually disagree with them.
 *
 * **`max_keys` is a C-only parameter.** The TypeScript and Python ports grow a
 * hash map without limit; C takes all its memory in `create` and never
 * allocates again. Once the table is full a key that has
 * never been seen is refused — fail-closed, because the alternative is to stop
 * limiting the moment an attacker cycles through keys.
 *
 * Memory: 16 bytes per tracked key, plus the map.
 */

#ifndef POLICYBOOK_RATE_LIMITER_SLIDING_COUNTER_H
#define POLICYBOOK_RATE_LIMITER_SLIDING_COUNTER_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_ratelimiter_sliding_counter_params {
    uint32_t limit;     /* requests allowed per window */
    uint32_t window_ms; /* window length, in milliseconds */
    uint32_t max_keys;  /* C only: keys tracked before new ones are refused */
} pb_ratelimiter_sliding_counter_params;

#define PB_RATELIMITER_SLIDING_COUNTER_PARAMS_DEFAULT { 100u, 1000u, 1024u }

extern const pb_ratelimiter_vtable pb_ratelimiter_sliding_counter;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_SLIDING_COUNTER_H */

/* ========================================================================
 * include/policybook/rate_limiter/sliding_log.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/sliding-log/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * SlidingLog — remember every request time, and count the recent ones.
 *
 * The only limiter in this domain that enforces its limit exactly: over any
 * window of `window_ms` ending at any instant, the number of admitted requests
 * is at most `limit`. No boundary effect, no estimate.
 *
 *     #include <policybook/rate_limiter/rate_limiter.h>
 *     #include <policybook/rate_limiter/sliding_log.h>
 *
 *     pb_ratelimiter_sliding_log_params params =
 *         PB_RATELIMITER_SLIDING_LOG_PARAMS_DEFAULT;
 *     pb_ratelimiter *limiter = pb_ratelimiter_sliding_log.create(&params, NULL, NULL);
 *     if (pb_ratelimiter_sliding_log.allow(limiter, key, 1u, now_ms)) { ... }
 *     pb_ratelimiter_sliding_log.destroy(limiter);
 *
 * The cost is memory, and in C it is visible in the signature rather than
 * hidden: the whole log is one allocation of `max_keys * limit` timestamps,
 * taken in `create`. At the default 100 permits over 1,024 keys that is 819,200
 * bytes. Check `memory_bytes` before choosing this policy on a constrained
 * target.
 *
 * **`max_keys` is a C-only parameter.** The TypeScript and Python ports grow a
 * hash map without limit; C takes all its memory in `create` and never
 * allocates again. Once the table is full a key that has
 * never been seen is refused — fail-closed, because the alternative is to stop
 * limiting the moment an attacker cycles through keys.
 *
 * Memory: 8 bytes per permit per tracked key, plus the map.
 */

#ifndef POLICYBOOK_RATE_LIMITER_SLIDING_LOG_H
#define POLICYBOOK_RATE_LIMITER_SLIDING_LOG_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_ratelimiter_sliding_log_params {
    uint32_t limit;     /* requests allowed in any window of window_ms */
    uint32_t window_ms; /* window length, in milliseconds */
    uint32_t max_keys;  /* C only: keys tracked before new ones are refused */
} pb_ratelimiter_sliding_log_params;

#define PB_RATELIMITER_SLIDING_LOG_PARAMS_DEFAULT { 100u, 1000u, 1024u }

extern const pb_ratelimiter_vtable pb_ratelimiter_sliding_log;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_SLIDING_LOG_H */

/* ========================================================================
 * include/policybook/rate_limiter/token_bucket.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/token-bucket/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * TokenBucket — spend from a balance that refills at a steady rate.
 *
 * The domain's recommended default. A key holds up to `burst` tokens; each
 * request spends one; the balance refills at `rate_per_sec` and stops at
 * `burst`. A long-run ceiling of `rate_per_sec`, plus the ability for a caller
 * who has been quiet to spend what it saved.
 *
 *     #include <policybook/rate_limiter/rate_limiter.h>
 *     #include <policybook/rate_limiter/token_bucket.h>
 *
 *     pb_ratelimiter_token_bucket_params params =
 *         PB_RATELIMITER_TOKEN_BUCKET_PARAMS_DEFAULT;
 *     pb_ratelimiter *limiter = pb_ratelimiter_token_bucket.create(&params, NULL, NULL);
 *     if (pb_ratelimiter_token_bucket.allow(limiter, key, 1u, now_ms)) { ... }
 *     pb_ratelimiter_token_bucket.destroy(limiter);
 *
 * The ledger is integer arithmetic: tokens are whole and the fraction lives in
 * a carry measured in thousandths of a token, matching the TypeScript and
 * Python ports exactly.
 *
 * At equal parameters this is `pb_ratelimiter_leaky_bucket` under the
 * substitution `tokens = capacity - level`. Both ship because the names are
 * what people look for; neither is faster.
 *
 * **`max_keys` is a C-only parameter.** The TypeScript and Python ports grow a
 * hash map without limit; C takes all its memory in `create` and never
 * allocates again. Once the table is full a key that has
 * never been seen is refused — fail-closed, because the alternative is to stop
 * limiting the moment an attacker cycles through keys.
 *
 * Memory: 24 bytes per tracked key, plus the map.
 */

#ifndef POLICYBOOK_RATE_LIMITER_TOKEN_BUCKET_H
#define POLICYBOOK_RATE_LIMITER_TOKEN_BUCKET_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_ratelimiter_token_bucket_params {
    uint32_t rate_per_sec; /* tokens added per second */
    uint32_t burst;        /* maximum tokens a key can hold */
    uint32_t max_keys;     /* C only: keys tracked before new ones are refused */
} pb_ratelimiter_token_bucket_params;

#define PB_RATELIMITER_TOKEN_BUCKET_PARAMS_DEFAULT { 100u, 100u, 1024u }

extern const pb_ratelimiter_vtable pb_ratelimiter_token_bucket;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_TOKEN_BUCKET_H */

/* ========================================================================
 * include/policybook/retry/retry.h
 * ======================================================================== */

/*
 * The `retry` domain: how long to wait before trying again.
 *
 * The smallest decision in the registry and the one most often got wrong. A
 * request failed; when should the client come back? Retry too eagerly and a
 * service that was merely slow becomes a service that is down, because every
 * client in the fleet is now hammering it in lockstep.
 *
 * Every policy exports a `const pb_retry_vtable` and a params struct with a
 * _DEFAULT initialiser:
 *
 *     #include <policybook/retry/retry.h>
 *     #include <policybook/retry/exponential_full_jitter.h>
 *
 *     pb_rng rng;
 *     pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
 *     pb_retry_exponential_full_jitter_params params =
 *         PB_RETRY_EXPONENTIAL_FULL_JITTER_PARAMS_DEFAULT;
 *
 *     pb_rng_init(&rng, 7u);
 *     pb_retry *policy = pb_retry_exponential_full_jitter.create(&params, NULL, &rng);
 *     int64_t delay = pb_retry_exponential_full_jitter.next_delay(policy, 1u, &error);
 *     if (delay == PB_RETRY_GIVE_UP) { ... }
 *     pb_retry_exponential_full_jitter.destroy(policy);
 *
 * The `pb_rng` is supplied at `create`, as it is for every other domain here.
 * threads it through the per-call function instead; putting it
 * on the hot path would buy nothing, and the property that matters — a policy
 * never reaching for a global source — is unchanged.
 */

#ifndef POLICYBOOK_RETRY_RETRY_H
#define POLICYBOOK_RETRY_RETRY_H



#ifdef __cplusplus
extern "C" {
#endif

/* Opaque per-policy state. */
typedef struct pb_retry pb_retry;

/*
 * Returned by `next_delay` when the policy has decided to stop.
 *
 * A decision, not an error. Negative so it can never collide with a delay,
 * which is why the function returns a signed type.
 */
#define PB_RETRY_GIVE_UP ((int64_t)-1)

/* What went wrong, as much of it as a policy is allowed to know. */
typedef struct pb_retry_error {
    int32_t status;  /* HTTP-style status, or 0 when there is none */
    bool retryable;  /* whether retrying could plausibly help at all */
    /*
     * How long the server asked the client to wait, in milliseconds.
     *
     * `has_retry_after` says whether the server said anything at all; most
     * errors carry no `Retry-After`, and "zero" is a different statement from
     * "nothing". A policy that ignores a server's own estimate is guessing when
     * it has been told.
     */
    bool has_retry_after;
    uint64_t retry_after_ms;
} pb_retry_error;

/* A retryable 503 carrying no Retry-After — the common case. */
#define PB_RETRY_ERROR_DEFAULT { 503, true, false, 0u }

typedef struct pb_retry_vtable {
    /*
     * Allocate the policy and everything it will ever need.
     *
     * `rng` may be NULL for a policy that draws nothing; a jittered policy
     * given NULL seeds itself deterministically rather than reaching for a
     * global source.
     */
    pb_retry *(*create)(const void *params, const pb_allocator *allocator, pb_rng *rng);

    /*
     * How long to wait before attempt `attempt + 1`, or PB_RETRY_GIVE_UP.
     *
     * `attempt` is 1-based: it is the number of the attempt that just failed,
     * so the first call always has `attempt == 1`.
     */
    int64_t (*next_delay)(pb_retry *policy, uint32_t attempt, const pb_retry_error *error);

    /* Bytes held by the policy, for the memory column. May be NULL. */
    size_t (*memory_bytes)(const pb_retry *policy);

    /* Release everything `create` took. Safe on NULL. */
    void (*destroy)(pb_retry *policy);
} pb_retry_vtable;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RETRY_RETRY_H */

/* ========================================================================
 * include/policybook/retry/traces.h
 * ======================================================================== */

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

/* ========================================================================
 * include/policybook/retry/constant.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/retry/constant/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * Constant — wait the same amount every time.
 *
 * The baseline, and the policy you get by accident when nobody thought about
 * it. It is the fastest to notice a *short* outage, and with no randomness
 * anywhere every client that failed together comes back together, forever.
 *
 *     #include <policybook/retry/retry.h>
 *     #include <policybook/retry/constant.h>
 *
 *     pb_retry_constant_params params = PB_RETRY_CONSTANT_PARAMS_DEFAULT;
 *     pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
 *     pb_retry *policy = pb_retry_constant.create(&params, NULL, NULL);
 *     int64_t delay = pb_retry_constant.next_delay(policy, 1u, &error);
 *     pb_retry_constant.destroy(policy);
 *
 * `rng` may be NULL: this policy draws nothing, which is precisely its defining
 * weakness.
 *
 * Memory: the struct alone. Nothing per attempt and nothing per key.
 */

#ifndef POLICYBOOK_RETRY_CONSTANT_H
#define POLICYBOOK_RETRY_CONSTANT_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_retry_constant_params {
    uint32_t base_ms;      /* the delay before every retry */
    uint32_t max_attempts; /* give up after this many attempts */
} pb_retry_constant_params;

#define PB_RETRY_CONSTANT_PARAMS_DEFAULT { 100u, 8u }

extern const pb_retry_vtable pb_retry_constant;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RETRY_CONSTANT_H */

/* ========================================================================
 * include/policybook/retry/decorrelated_jitter.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/retry/decorrelated-jitter/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * DecorrelatedJitter — grow the delay from the last delay, not the attempt number.
 *
 * The other policies in this domain compute their delay from `attempt`, so a
 * client's whole schedule is determined the moment it starts failing. This one
 * is a random walk instead:
 *
 *     delay = min(cap, base + next_int(prev * 3 - base + 1))
 *     prev  = delay
 *
 * `prev` is state, and it is the whole idea: the delay depends on the history
 * of the retry sequence rather than on its length, so whole schedules diverge
 * rather than individual attempts. This is the only policy in the domain that
 * remembers anything between calls.
 *
 *     #include <policybook/retry/retry.h>
 *     #include <policybook/retry/decorrelated_jitter.h>
 *
 *     pb_rng rng;
 *     pb_retry_decorrelated_jitter_params params =
 *         PB_RETRY_DECORRELATED_JITTER_PARAMS_DEFAULT;
 *     pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
 *
 *     pb_rng_init(&rng, 7u);
 *     pb_retry *policy = pb_retry_decorrelated_jitter.create(&params, NULL, &rng);
 *     int64_t delay = pb_retry_decorrelated_jitter.next_delay(policy, 1u, &error);
 *     pb_retry_decorrelated_jitter.destroy(policy);
 *
 * Because the walk lives in the policy object, its lifetime *is* the retry
 * sequence: a policy created per attempt restarts at `base` every time and
 * degenerates into a fixed-range draw.
 *
 * It climbs more slowly than doubling despite reaching for three times the last
 * delay — the draw is uniform over that range, so the expected step is about
 * 1.5x against exponential's exact 2x. The variance is the point, not the speed.
 *
 * Memory: the struct alone, holding one integer of walk state.
 */

#ifndef POLICYBOOK_RETRY_DECORRELATED_JITTER_H
#define POLICYBOOK_RETRY_DECORRELATED_JITTER_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_retry_decorrelated_jitter_params {
    uint32_t base_ms;      /* the floor of every delay, and where the walk starts */
    uint32_t cap_ms;       /* no delay exceeds this */
    uint32_t max_attempts; /* give up after this many attempts */
} pb_retry_decorrelated_jitter_params;

#define PB_RETRY_DECORRELATED_JITTER_PARAMS_DEFAULT { 100u, 10000u, 8u }

extern const pb_retry_vtable pb_retry_decorrelated_jitter;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RETRY_DECORRELATED_JITTER_H */

/* ========================================================================
 * include/policybook/retry/equal_jitter.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/retry/equal-jitter/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * EqualJitter — half the exponential delay fixed, half of it random.
 *
 * The middle ground between `pb_retry_exponential`, which spreads nothing, and
 * `pb_retry_exponential_full_jitter`, which spreads everything and halves the
 * expected wait in the process:
 *
 *     half  = min(cap, base * 2^(attempt-1)) / 2
 *     delay = half + next_int(half + 1)
 *
 * A delay always lands in [half, 2 x half] and averages about three quarters of
 * the un-jittered ceiling. Choose it when the backoff must actually back off:
 * full jitter can return zero on any attempt, and this cannot fall below half
 * the ceiling.
 *
 *     #include <policybook/retry/retry.h>
 *     #include <policybook/retry/equal_jitter.h>
 *
 *     pb_rng rng;
 *     pb_retry_equal_jitter_params params = PB_RETRY_EQUAL_JITTER_PARAMS_DEFAULT;
 *     pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
 *
 *     pb_rng_init(&rng, 7u);
 *     pb_retry *policy = pb_retry_equal_jitter.create(&params, NULL, &rng);
 *     int64_t delay = pb_retry_equal_jitter.next_delay(policy, 1u, &error);
 *     pb_retry_equal_jitter.destroy(policy);
 *
 * The halving is integer division: at a ceiling of 1 the half is 0 and every
 * delay is 0. Degenerate, and documented rather than special-cased, because a
 * special case would be a different policy.
 *
 * Memory: the struct alone.
 */

#ifndef POLICYBOOK_RETRY_EQUAL_JITTER_H
#define POLICYBOOK_RETRY_EQUAL_JITTER_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_retry_equal_jitter_params {
    uint32_t base_ms;      /* the first ceiling, doubled on each attempt */
    uint32_t cap_ms;       /* no ceiling exceeds this */
    uint32_t max_attempts; /* give up after this many attempts */
} pb_retry_equal_jitter_params;

#define PB_RETRY_EQUAL_JITTER_PARAMS_DEFAULT { 100u, 10000u, 8u }

extern const pb_retry_vtable pb_retry_equal_jitter;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RETRY_EQUAL_JITTER_H */

/* ========================================================================
 * include/policybook/retry/exponential.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/retry/exponential/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * Exponential — double the wait after every failure, up to a cap.
 *
 * The textbook answer, and a genuine improvement on constant backoff: load on a
 * struggling service falls off geometrically as an outage continues.
 *
 * It still synchronises clients, and that is the reason not to ship it. The
 * delay is a pure function of the attempt number, so every client that failed
 * at the same moment retries at the same moment. Backing off exponentially
 * converts a continuous herd into a periodic one; it does not disperse it.
 * `pb_retry_exponential_full_jitter` is this plus one draw.
 *
 *     #include <policybook/retry/retry.h>
 *     #include <policybook/retry/exponential.h>
 *
 *     pb_retry_exponential_params params = PB_RETRY_EXPONENTIAL_PARAMS_DEFAULT;
 *     pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
 *     pb_retry *policy = pb_retry_exponential.create(&params, NULL, NULL);
 *     int64_t delay = pb_retry_exponential.next_delay(policy, 3u, &error);
 *     pb_retry_exponential.destroy(policy);
 *
 * `rng` may be NULL: no draw anywhere, which is why it synchronises.
 *
 * Memory: the struct alone.
 */

#ifndef POLICYBOOK_RETRY_EXPONENTIAL_H
#define POLICYBOOK_RETRY_EXPONENTIAL_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_retry_exponential_params {
    uint32_t base_ms;      /* the first delay, doubled on each attempt */
    uint32_t cap_ms;       /* no delay exceeds this */
    uint32_t max_attempts; /* give up after this many attempts */
} pb_retry_exponential_params;

#define PB_RETRY_EXPONENTIAL_PARAMS_DEFAULT { 100u, 10000u, 8u }

extern const pb_retry_vtable pb_retry_exponential;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RETRY_EXPONENTIAL_H */

/* ========================================================================
 * include/policybook/retry/exponential_full_jitter.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/retry/exponential-full-jitter/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * ExponentialFullJitter — a uniform delay between zero and the exponential ceiling.
 *
 * The domain's recommended default. It keeps everything exponential backoff
 * gets right — load falls geometrically as an outage continues — and fixes the
 * thing it gets wrong: clients no longer retry in lockstep, because each picks
 * its own delay.
 *
 *     #include <policybook/retry/retry.h>
 *     #include <policybook/retry/exponential_full_jitter.h>
 *
 *     pb_rng rng;
 *     pb_retry_exponential_full_jitter_params params =
 *         PB_RETRY_EXPONENTIAL_FULL_JITTER_PARAMS_DEFAULT;
 *     pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
 *
 *     pb_rng_init(&rng, 7u);
 *     pb_retry *policy = pb_retry_exponential_full_jitter.create(&params, NULL, &rng);
 *     int64_t delay = pb_retry_exponential_full_jitter.next_delay(policy, 1u, &error);
 *     pb_retry_exponential_full_jitter.destroy(policy);
 *
 * The expected delay is half the un-jittered ceiling, so this is *more*
 * aggressive than plain exponential rather than less. It wins anyway, because
 * spreading a fleet matters more than the average wait.
 *
 * The `pb_rng` is borrowed, not owned: the caller keeps it alive for the
 * policy's lifetime. Passing NULL seeds a deterministic stream rather than
 * reaching for a global source.
 *
 * Memory: the struct alone.
 */

#ifndef POLICYBOOK_RETRY_EXPONENTIAL_FULL_JITTER_H
#define POLICYBOOK_RETRY_EXPONENTIAL_FULL_JITTER_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_retry_exponential_full_jitter_params {
    uint32_t base_ms;      /* the first ceiling, doubled on each attempt */
    uint32_t cap_ms;       /* no ceiling exceeds this */
    uint32_t max_attempts; /* give up after this many attempts */
} pb_retry_exponential_full_jitter_params;

#define PB_RETRY_EXPONENTIAL_FULL_JITTER_PARAMS_DEFAULT { 100u, 10000u, 8u }

extern const pb_retry_vtable pb_retry_exponential_full_jitter;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RETRY_EXPONENTIAL_FULL_JITTER_H */

/* ========================================================================
 * include/policybook/retry/retry_after_aware.h
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/retry/retry-after-aware/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * RetryAfterAware — do what the server asked, and guess only when it did not.
 *
 * Every other policy in this domain is guessing. This one reads the answer when
 * the server has provided it, and falls back to full jitter when it has not:
 *
 *     if error->has_retry_after:  delay = min(cap, error->retry_after_ms)
 *     otherwise:                  delay = next_int(ceiling + 1)
 *
 *     #include <policybook/retry/retry.h>
 *     #include <policybook/retry/retry_after_aware.h>
 *
 *     pb_rng rng;
 *     pb_retry_retry_after_aware_params params =
 *         PB_RETRY_RETRY_AFTER_AWARE_PARAMS_DEFAULT;
 *     pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
 *
 *     error.has_retry_after = true;
 *     error.retry_after_ms = 2500u;
 *     pb_rng_init(&rng, 7u);
 *     pb_retry *policy = pb_retry_retry_after_aware.create(&params, NULL, &rng);
 *     int64_t delay = pb_retry_retry_after_aware.next_delay(policy, 1u, &error);
 *     pb_retry_retry_after_aware.destroy(policy);
 *
 * `has_retry_after` is what distinguishes "the server said nothing" from "the
 * server said zero". Zero is an instruction — come back now — and is honoured.
 *
 * **The clamp is not a formality.** A server under load can ask for minutes,
 * and a client that honours an arbitrary hint has handed a stranger control of
 * its own latency budget. `cap_ms` is the caller's statement of how long it is
 * willing to be told to wait.
 *
 * **This policy re-synchronises clients by construction.** A thousand told to
 * come back in five seconds all come back in five seconds — precisely the herd
 * jitter exists to break. See README.md for when that trade is worth making.
 *
 * Memory: the struct alone.
 */

#ifndef POLICYBOOK_RETRY_RETRY_AFTER_AWARE_H
#define POLICYBOOK_RETRY_RETRY_AFTER_AWARE_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_retry_retry_after_aware_params {
    uint32_t base_ms;      /* the first fallback ceiling, doubled each attempt */
    uint32_t cap_ms;       /* the longest wait this client accepts, from any source */
    uint32_t max_attempts; /* give up after this many attempts */
} pb_retry_retry_after_aware_params;

#define PB_RETRY_RETRY_AFTER_AWARE_PARAMS_DEFAULT { 100u, 10000u, 8u }

extern const pb_retry_vtable pb_retry_retry_after_aware;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RETRY_RETRY_AFTER_AWARE_H */

/* ========================================================================
 * include/policybook/policybook.h
 * ======================================================================== */

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

/* Data structures. */

/* Domain: cache. */

/* Domain: kv-cache. */

/* Domain: rate-limiter. */

/* Domain: retry. */

#endif /* POLICYBOOK_H */

#endif /* POLICYBOOK_AMALGAMATED_H */

/*
 * The implementation. Define POLICYBOOK_IMPLEMENTATION in exactly one
 * translation unit before including this file; every other include gets the
 * declarations above and nothing else.
 */
#ifdef POLICYBOOK_IMPLEMENTATION
#ifndef POLICYBOOK_AMALGAMATED_IMPLEMENTATION
#define POLICYBOOK_AMALGAMATED_IMPLEMENTATION

/* ========================================================================
 * src/allocator.c
 * ======================================================================== */

/* The only translation unit permitted to include <stdlib.h>. */

static void *pb_default_alloc(void *ctx, size_t n)
{
    (void)ctx;
    return malloc(n);
}

static void pb_default_free(void *ctx, void *p, size_t n)
{
    (void)ctx;
    (void)n;
    free(p);
}

static const pb_allocator pb_default_allocator = {
    pb_default_alloc,
    pb_default_free,
    NULL,
};

const pb_allocator *pb_allocator_default(void)
{
    return &pb_default_allocator;
}

/* ========================================================================
 * src/cache/2q.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/2q/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * 2Q — admit to the main cache only on a second access.
 *
 * Mirrors index.ts and policy.py. Three structures: A1in as a FIFO ring of
 * slots, Am as an index-based LRU list, and A1out as a FIFO list of keys with
 * a membership map — a list, not a ring, because promotion removes a ghost
 * from the middle and the remaining ghosts must keep both their order and
 * their claim on A1out's capacity. Everything is taken in create.
 */




typedef struct pb_cache_2q_state {
    const pb_allocator *allocator;
    uint32_t capacity;
    uint32_t slot_count; /* capacity + 1: a caller inserts before evicting */
    uint32_t kin_size;
    uint32_t kout_size;

    pb_map index;   /* resident key -> slot */
    uint64_t *keys; /* slot -> key */
    uint8_t *in_main;

    pb_ring admission; /* A1in: slots in arrival order */
    pb_ilist main;     /* Am: head is most recently used */
    uint32_t main_length;

    /* A1out: keys with no values behind them, newest at the list head. */
    uint64_t *ghost_keys;   /* ghost slot -> key */
    pb_ilist ghost_list;    /* order; the tail is the oldest ghost */
    pb_map ghost_index;     /* ghost key -> its slot */
    uint32_t *ghost_free;   /* unused ghost slots */
    uint32_t ghost_free_count;

    uint32_t *free_slots;
    uint32_t free_count;
} pb_cache_2q_state;

static void two_q_release(pb_cache_2q_state *self)
{
    const pb_allocator *allocator = self->allocator;
    size_t slots = (size_t)self->slot_count;

    pb_map_destroy(&self->index, allocator);
    pb_map_destroy(&self->ghost_index, allocator);
    pb_ring_destroy(&self->admission, allocator);
    pb_ilist_destroy(&self->main, allocator);
    pb_ilist_destroy(&self->ghost_list, allocator);
    pb_free(allocator, self->keys, slots * sizeof(uint64_t));
    pb_free(allocator, self->in_main, slots * sizeof(uint8_t));
    pb_free(allocator, self->free_slots, slots * sizeof(uint32_t));
    pb_free(allocator, self->ghost_keys, (size_t)self->kout_size * sizeof(uint64_t));
    pb_free(allocator, self->ghost_free, (size_t)self->kout_size * sizeof(uint32_t));
    pb_free(allocator, self, sizeof(pb_cache_2q_state));
}

static pb_cache *two_q_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_2q_params *config = (const pb_cache_2q_params *)params;
    pb_cache_2q_state *self;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t slot;
    double kin;
    double kout;

    (void)rng; /* 2Q makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    kin = (config == NULL) ? 0.25 : config->kin;
    kout = (config == NULL) ? 0.5 : config->kout;

    if (capacity == 0u || capacity == UINT32_MAX) {
        return NULL;
    }
    if (!(kin > 0.0) || kin > 1.0 || !(kout > 0.0) || kout > 1.0) {
        return NULL;
    }
    slot_count = capacity + 1u;

    self = (pb_cache_2q_state *)pb_alloc(allocator, sizeof(pb_cache_2q_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    /* Both fractions floor to at least one entry. */
    self->kin_size = (uint32_t)((double)capacity * kin);
    if (self->kin_size == 0u) {
        self->kin_size = 1u;
    }
    self->kout_size = (uint32_t)((double)capacity * kout);
    if (self->kout_size == 0u) {
        self->kout_size = 1u;
    }

    self->index.capacity = 0;
    self->ghost_index.capacity = 0;
    self->admission.capacity = 0;
    self->main.capacity = 0;
    self->ghost_list.capacity = 0;
    self->main_length = 0;
    self->ghost_free_count = 0;
    self->free_count = 0;

    self->keys = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    self->in_main = (uint8_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint8_t));
    self->free_slots = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->ghost_keys =
        (uint64_t *)pb_alloc(allocator, (size_t)self->kout_size * sizeof(uint64_t));
    self->ghost_free =
        (uint32_t *)pb_alloc(allocator, (size_t)self->kout_size * sizeof(uint32_t));

    if (self->keys == NULL || self->in_main == NULL || self->free_slots == NULL ||
        self->ghost_keys == NULL || self->ghost_free == NULL ||
        !pb_map_init(&self->index, slot_count, allocator) ||
        !pb_map_init(&self->ghost_index, self->kout_size, allocator) ||
        !pb_ring_init(&self->admission, slot_count, allocator) ||
        !pb_ilist_init(&self->main, slot_count, allocator) ||
        !pb_ilist_init(&self->ghost_list, self->kout_size, allocator)) {
        two_q_release(self);
        return NULL;
    }

    for (slot = 0; slot < slot_count; ++slot) {
        self->in_main[slot] = 0u;
        self->free_slots[slot] = slot_count - 1u - slot;
    }
    self->free_count = slot_count;
    for (slot = 0; slot < self->kout_size; ++slot) {
        self->ghost_free[slot] = self->kout_size - 1u - slot;
    }
    self->ghost_free_count = self->kout_size;

    return (pb_cache *)self;
}

static void two_q_destroy(pb_cache *cache)
{
    pb_cache_2q_state *self = (pb_cache_2q_state *)cache;

    if (self == NULL) {
        return;
    }
    two_q_release(self);
}

static void two_q_remember_ghost(pb_cache_2q_state *self, uint64_t key)
{
    uint32_t ghost_slot;

    if (self->ghost_list.length == self->kout_size) {
        /* A key whose ghost has expired has to start from A1in again. */
        ghost_slot = pb_ilist_pop_back(&self->ghost_list);
        (void)pb_map_remove(&self->ghost_index, self->ghost_keys[ghost_slot]);
    } else {
        self->ghost_free_count -= 1u;
        ghost_slot = self->ghost_free[self->ghost_free_count];
    }

    self->ghost_keys[ghost_slot] = key;
    pb_ilist_push_front(&self->ghost_list, ghost_slot);
    (void)pb_map_put(&self->ghost_index, key, ghost_slot);
}

static void two_q_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_2q_state *self = (pb_cache_2q_state *)cache;
    uint32_t slot;
    uint32_t ghost_slot;

    (void)meta;
    assert(self != NULL);

    if (hit) {
        if (!pb_map_get(&self->index, key, &slot)) {
            assert(false); /* the caller's residency tracking is wrong */
            return;
        }
        /*
         * A hit in Am refreshes recency. A hit in A1in does nothing at all: the
         * key has not yet earned promotion, and reordering A1in would make it a
         * second LRU rather than an audition.
         */
        if (self->in_main[slot] != 0u) {
            pb_ilist_move_to_front(&self->main, slot);
        }
        return;
    }

    assert(self->free_count > 0u);
    if (self->free_count == 0u) {
        return;
    }

    self->free_count -= 1u;
    slot = self->free_slots[self->free_count];
    self->keys[slot] = key;
    (void)pb_map_put(&self->index, key, slot);

    if (pb_map_get(&self->ghost_index, key, &ghost_slot)) {
        /* The second access the policy has been waiting for. Promotion removes
         * exactly this ghost; the others keep their order and their claim on
         * A1out's capacity. */
        (void)pb_map_remove(&self->ghost_index, key);
        pb_ilist_remove(&self->ghost_list, ghost_slot);
        self->ghost_free[self->ghost_free_count] = ghost_slot;
        self->ghost_free_count += 1u;
        self->in_main[slot] = 1u;
        pb_ilist_push_front(&self->main, slot);
        self->main_length += 1u;
        return;
    }

    self->in_main[slot] = 0u;
    (void)pb_ring_push_back(&self->admission, slot);
}

static uint64_t two_q_evict(pb_cache *cache)
{
    pb_cache_2q_state *self = (pb_cache_2q_state *)cache;
    uint32_t slot;
    uint64_t key;
    bool take_from_admission;

    assert(self != NULL);

    /* Drain A1in while it is over its share. */
    take_from_admission = self->admission.length > 0u &&
                          (self->admission.length > self->kin_size || self->main_length == 0u);

    if (take_from_admission) {
        slot = pb_ring_pop_front(&self->admission);
        key = self->keys[slot];
        two_q_remember_ghost(self, key);
    } else {
        if (self->main_length == 0u) {
            assert(false);
            return 0u;
        }
        slot = pb_ilist_pop_back(&self->main);
        if (slot == PB_ILIST_NIL) {
            return 0u;
        }
        self->main_length -= 1u;
        key = self->keys[slot];
        /* No ghost: an Am entry has already proven itself once. */
    }

    (void)pb_map_remove(&self->index, key);
    self->in_main[slot] = 0u;
    self->free_slots[self->free_count] = slot;
    self->free_count += 1u;
    return key;
}

static size_t two_q_memory_bytes(const pb_cache *cache)
{
    const pb_cache_2q_state *self = (const pb_cache_2q_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_2q_state) +
           (size_t)self->slot_count * (sizeof(uint64_t) + sizeof(uint8_t) + sizeof(uint32_t)) +
           (size_t)self->kout_size * (sizeof(uint64_t) + sizeof(uint32_t)) +
           pb_map_memory_bytes(&self->index) + pb_map_memory_bytes(&self->ghost_index) +
           pb_ring_memory_bytes(&self->admission) + pb_ilist_memory_bytes(&self->main) +
           pb_ilist_memory_bytes(&self->ghost_list);
}

const pb_cache_vtable pb_cache_2q = {
    two_q_create,
    two_q_on_access,
    two_q_evict,
    NULL, /* admits everything */
    two_q_destroy,
    two_q_memory_bytes,
    false, /* allocates_after_create */
    "cache/2q"
};

/* ========================================================================
 * src/cache/arc.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/arc/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * ARC — balance recency against frequency, and tune the balance itself.
 *
 * Mirrors index.ts and policy.py, implemented from the paper's Figure 4. One
 * slot pool serves all four lists, since a key is in exactly one of them; the
 * pool is sized 2c + 2 because every cached entry may also have a ghost.
 */




#define PB_ARC_NIL 0xFFFFFFFFu

/* The four lists, used to index the head/tail/length arrays. */
#define PB_ARC_T1 0u
#define PB_ARC_T2 1u
#define PB_ARC_B1 2u
#define PB_ARC_B2 3u
#define PB_ARC_LISTS 4u

typedef struct pb_cache_arc_state {
    const pb_allocator *allocator;
    uint32_t capacity;
    uint32_t slot_count;

    pb_map index; /* key -> slot, across all four lists */
    uint64_t *keys;

    uint32_t *next;
    uint32_t *prev;
    uint8_t *list_of;
    uint32_t heads[PB_ARC_LISTS];
    uint32_t tails[PB_ARC_LISTS];
    uint32_t lengths[PB_ARC_LISTS];

    uint32_t *free_slots;
    uint32_t free_count;

    uint32_t target; /* p: the target size for T1 */

    bool has_victim;
    uint64_t victim;
} pb_cache_arc_state;

static void arc_release(pb_cache_arc_state *self)
{
    const pb_allocator *allocator = self->allocator;
    size_t slots = (size_t)self->slot_count;

    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self->keys, slots * sizeof(uint64_t));
    pb_free(allocator, self->next, slots * sizeof(uint32_t));
    pb_free(allocator, self->prev, slots * sizeof(uint32_t));
    pb_free(allocator, self->list_of, slots * sizeof(uint8_t));
    pb_free(allocator, self->free_slots, slots * sizeof(uint32_t));
    pb_free(allocator, self, sizeof(pb_cache_arc_state));
}

static pb_cache *arc_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_arc_params *config = (const pb_cache_arc_params *)params;
    pb_cache_arc_state *self;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t slot;
    uint32_t list;

    (void)rng; /* ARC makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    if (capacity == 0u || capacity > (UINT32_MAX - 2u) / 2u) {
        return NULL;
    }
    /* The cache holds at most c entries and the ghosts at most c more, plus a
     * spare for the entry in flight during a miss. */
    slot_count = 2u * capacity + 2u;

    self = (pb_cache_arc_state *)pb_alloc(allocator, sizeof(pb_cache_arc_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    self->index.capacity = 0;
    self->free_count = 0;
    self->target = 0;
    self->has_victim = false;
    self->victim = 0;

    self->keys = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    self->next = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->prev = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->list_of = (uint8_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint8_t));
    self->free_slots = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));

    if (self->keys == NULL || self->next == NULL || self->prev == NULL ||
        self->list_of == NULL || self->free_slots == NULL ||
        !pb_map_init(&self->index, slot_count, allocator)) {
        arc_release(self);
        return NULL;
    }

    for (slot = 0; slot < slot_count; ++slot) {
        self->next[slot] = PB_ARC_NIL;
        self->prev[slot] = PB_ARC_NIL;
        self->list_of[slot] = (uint8_t)PB_ARC_LISTS; /* in no list */
        self->free_slots[slot] = slot_count - 1u - slot;
    }
    self->free_count = slot_count;

    for (list = 0; list < PB_ARC_LISTS; ++list) {
        self->heads[list] = PB_ARC_NIL;
        self->tails[list] = PB_ARC_NIL;
        self->lengths[list] = 0;
    }

    return (pb_cache *)self;
}

static void arc_destroy(pb_cache *cache)
{
    pb_cache_arc_state *self = (pb_cache_arc_state *)cache;

    if (self == NULL) {
        return;
    }
    arc_release(self);
}

static void arc_link_front(pb_cache_arc_state *self, uint32_t list, uint32_t slot)
{
    uint32_t head = self->heads[list];

    self->prev[slot] = PB_ARC_NIL;
    self->next[slot] = head;
    if (head != PB_ARC_NIL) {
        self->prev[head] = slot;
    } else {
        self->tails[list] = slot;
    }
    self->heads[list] = slot;
    self->list_of[slot] = (uint8_t)list;
    self->lengths[list] += 1u;
}

static void arc_unlink(pb_cache_arc_state *self, uint32_t slot)
{
    uint32_t list = self->list_of[slot];
    uint32_t following = self->next[slot];
    uint32_t preceding = self->prev[slot];

    assert(list < PB_ARC_LISTS);

    if (preceding != PB_ARC_NIL) {
        self->next[preceding] = following;
    } else {
        self->heads[list] = following;
    }
    if (following != PB_ARC_NIL) {
        self->prev[following] = preceding;
    } else {
        self->tails[list] = preceding;
    }

    self->next[slot] = PB_ARC_NIL;
    self->prev[slot] = PB_ARC_NIL;
    self->list_of[slot] = (uint8_t)PB_ARC_LISTS;
    self->lengths[list] -= 1u;
}

static uint32_t arc_take_slot(pb_cache_arc_state *self, uint64_t key)
{
    uint32_t slot;

    assert(self->free_count > 0u);
    self->free_count -= 1u;
    slot = self->free_slots[self->free_count];
    self->keys[slot] = key;
    (void)pb_map_put(&self->index, key, slot);
    return slot;
}

static void arc_release_slot(pb_cache_arc_state *self, uint32_t slot)
{
    (void)pb_map_remove(&self->index, self->keys[slot]);
    self->free_slots[self->free_count] = slot;
    self->free_count += 1u;
}

/* Remove the oldest entry of a cache list, optionally leaving a ghost. */
static void arc_evict_oldest(pb_cache_arc_state *self, uint32_t list, uint32_t ghost)
{
    uint32_t slot = self->tails[list];

    assert(slot != PB_ARC_NIL);
    if (slot == PB_ARC_NIL) {
        return;
    }

    self->victim = self->keys[slot];
    self->has_victim = true;

    arc_unlink(self, slot);
    if (ghost >= PB_ARC_LISTS) {
        arc_release_slot(self, slot);
    } else {
        arc_link_front(self, ghost, slot);
    }
}

static void arc_drop_oldest_ghost(pb_cache_arc_state *self, uint32_t list)
{
    uint32_t slot = self->tails[list];

    if (slot == PB_ARC_NIL) {
        return;
    }
    arc_unlink(self, slot);
    arc_release_slot(self, slot);
}

/*
 * The paper's REPLACE: choose a victim from T1 or T2 and demote it to the
 * matching ghost list.
 *
 * T1 gives up an entry when it is over its target — or when it is exactly at
 * target and the key that caused this came back from B2, which is a hint that
 * the frequent side deserves the benefit of the doubt.
 */
static void arc_replace(pb_cache_arc_state *self, bool returning_from_b2)
{
    uint32_t t1 = self->lengths[PB_ARC_T1];
    bool take_from_t1 =
        t1 >= 1u && ((returning_from_b2 && t1 == self->target) || t1 > self->target);

    if (take_from_t1) {
        arc_evict_oldest(self, PB_ARC_T1, PB_ARC_B1);
    } else {
        arc_evict_oldest(self, PB_ARC_T2, PB_ARC_B2);
    }
}

static void arc_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_arc_state *self = (pb_cache_arc_state *)cache;
    uint32_t slot;
    uint32_t list;
    uint32_t b1;
    uint32_t b2;
    uint32_t t1;
    uint32_t total;
    bool known;

    (void)meta;
    assert(self != NULL);

    known = pb_map_get(&self->index, key, &slot);

    if (hit) {
        if (!known || self->list_of[slot] > (uint8_t)PB_ARC_T2) {
            assert(false); /* the caller's residency tracking is wrong */
            return;
        }
        /* Case I: a second recent use promotes the key to the frequent list. */
        arc_unlink(self, slot);
        arc_link_front(self, PB_ARC_T2, slot);
        return;
    }

    b1 = self->lengths[PB_ARC_B1];
    b2 = self->lengths[PB_ARC_B2];

    if (known) {
        list = self->list_of[slot];
        assert(list == PB_ARC_B1 || list == PB_ARC_B2);

        if (list == PB_ARC_B1) {
            /* Case II: recency was undervalued, so give T1 more room. */
            uint32_t delta = (b1 >= b2) ? 1u : (b2 / b1);
            self->target += delta;
            if (self->target > self->capacity) {
                self->target = self->capacity;
            }
            arc_replace(self, false);
        } else {
            /* Case III: frequency was undervalued, so take room away from T1. */
            uint32_t delta = (b2 >= b1) ? 1u : (b1 / b2);
            self->target = (self->target > delta) ? (self->target - delta) : 0u;
            arc_replace(self, true);
        }

        arc_unlink(self, slot);
        arc_link_front(self, PB_ARC_T2, slot);
        return;
    }

    /* Case IV: a key ARC has no memory of at all. */
    t1 = self->lengths[PB_ARC_T1];
    total = t1 + self->lengths[PB_ARC_T2] + b1 + b2;

    if (t1 + b1 == self->capacity) {
        if (t1 < self->capacity) {
            arc_drop_oldest_ghost(self, PB_ARC_B1);
            arc_replace(self, false);
        } else {
            /* No room for a ghost: |T1| + |B1| <= c is the invariant. */
            arc_evict_oldest(self, PB_ARC_T1, PB_ARC_LISTS);
        }
    } else if (t1 + b1 < self->capacity && total >= self->capacity) {
        if (total == 2u * self->capacity) {
            arc_drop_oldest_ghost(self, PB_ARC_B2);
        }
        arc_replace(self, false);
    }

    slot = arc_take_slot(self, key);
    arc_link_front(self, PB_ARC_T1, slot);
}

static uint64_t arc_evict(pb_cache *cache)
{
    pb_cache_arc_state *self = (pb_cache_arc_state *)cache;

    assert(self != NULL);
    assert(self->has_victim);
    if (!self->has_victim) {
        return 0u;
    }
    self->has_victim = false;
    return self->victim;
}

static size_t arc_memory_bytes(const pb_cache *cache)
{
    const pb_cache_arc_state *self = (const pb_cache_arc_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_arc_state) +
           (size_t)self->slot_count *
               (sizeof(uint64_t) + 3u * sizeof(uint32_t) + sizeof(uint8_t)) +
           pb_map_memory_bytes(&self->index);
}

const pb_cache_vtable pb_cache_arc = {
    arc_create,
    arc_on_access,
    arc_evict,
    NULL, /* admits everything */
    arc_destroy,
    arc_memory_bytes,
    false, /* allocates_after_create */
    "cache/arc"
};

/* ========================================================================
 * src/cache/clock.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/clock/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * CLOCK — approximate LRU with one reference bit per entry.
 *
 * Mirrors index.ts and policy.py: the queue formulation, which is the same
 * algorithm as the circular buffer with a rotating hand. The ring's front is
 * the hand.
 */




typedef struct pb_cache_clock_state {
    const pb_allocator *allocator;
    uint32_t capacity;
    uint32_t slot_count; /* capacity + 1: a caller inserts before evicting */

    pb_map index;      /* key -> slot */
    pb_ring order;     /* slot indices in arrival order; the front is the hand */
    uint64_t *keys;    /* slot -> key */
    uint8_t *referenced; /* slot -> reference bit */

    uint32_t *free_slots;
    uint32_t free_count;
} pb_cache_clock_state;

static void clock_release(pb_cache_clock_state *self)
{
    const pb_allocator *allocator = self->allocator;
    size_t slots = (size_t)self->slot_count;

    pb_map_destroy(&self->index, allocator);
    pb_ring_destroy(&self->order, allocator);
    pb_free(allocator, self->keys, slots * sizeof(uint64_t));
    pb_free(allocator, self->referenced, slots * sizeof(uint8_t));
    pb_free(allocator, self->free_slots, slots * sizeof(uint32_t));
    pb_free(allocator, self, sizeof(pb_cache_clock_state));
}

static pb_cache *clock_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_clock_params *config = (const pb_cache_clock_params *)params;
    pb_cache_clock_state *self;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t slot;

    (void)rng; /* CLOCK makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    if (capacity == 0u || capacity == UINT32_MAX) {
        return NULL;
    }
    slot_count = capacity + 1u;

    self = (pb_cache_clock_state *)pb_alloc(allocator, sizeof(pb_cache_clock_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    self->free_count = 0;
    self->index.capacity = 0;
    self->order.capacity = 0;
    self->keys = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    self->referenced = (uint8_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint8_t));
    self->free_slots = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));

    if (self->keys == NULL || self->referenced == NULL || self->free_slots == NULL ||
        !pb_map_init(&self->index, slot_count, allocator) ||
        !pb_ring_init(&self->order, slot_count, allocator)) {
        clock_release(self);
        return NULL;
    }

    for (slot = 0; slot < slot_count; ++slot) {
        self->referenced[slot] = 0u;
        self->free_slots[slot] = slot_count - 1u - slot;
    }
    self->free_count = slot_count;

    return (pb_cache *)self;
}

static void clock_destroy(pb_cache *cache)
{
    pb_cache_clock_state *self = (pb_cache_clock_state *)cache;

    if (self == NULL) {
        return;
    }
    clock_release(self);
}

static void clock_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_clock_state *self = (pb_cache_clock_state *)cache;
    uint32_t slot;

    (void)meta;
    assert(self != NULL);

    if (hit) {
        if (!pb_map_get(&self->index, key, &slot)) {
            assert(false); /* the caller's residency tracking is wrong */
            return;
        }
        /* The entire hit path: set one bit. No reordering, no shared writes. */
        self->referenced[slot] = 1u;
        return;
    }

    assert(self->free_count > 0u);
    if (self->free_count == 0u) {
        return;
    }

    self->free_count -= 1u;
    slot = self->free_slots[self->free_count];
    self->keys[slot] = key;
    self->referenced[slot] = 0u;
    (void)pb_map_put(&self->index, key, slot);
    (void)pb_ring_push_back(&self->order, slot);
}

static uint64_t clock_evict(pb_cache *cache)
{
    pb_cache_clock_state *self = (pb_cache_clock_state *)cache;

    assert(self != NULL);
    assert(!pb_ring_is_empty(&self->order));

    /* The hand can pass each entry at most once, because passing it clears
     * that entry's bit. */
    for (;;) {
        uint32_t slot = pb_ring_pop_front(&self->order);
        uint64_t key;

        if (slot == PB_RING_NIL) {
            return 0u;
        }

        if (self->referenced[slot] != 0u) {
            self->referenced[slot] = 0u; /* second chance */
            (void)pb_ring_push_back(&self->order, slot);
            continue;
        }

        key = self->keys[slot];
        (void)pb_map_remove(&self->index, key);
        self->free_slots[self->free_count] = slot;
        self->free_count += 1u;
        return key;
    }
}

static size_t clock_memory_bytes(const pb_cache *cache)
{
    const pb_cache_clock_state *self = (const pb_cache_clock_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_clock_state) +
           (size_t)self->slot_count * (sizeof(uint64_t) + sizeof(uint8_t) + sizeof(uint32_t)) +
           pb_map_memory_bytes(&self->index) + pb_ring_memory_bytes(&self->order);
}

const pb_cache_vtable pb_cache_clock = {
    clock_create,
    clock_on_access,
    clock_evict,
    NULL, /* admits everything */
    clock_destroy,
    clock_memory_bytes,
    false, /* allocates_after_create */
    "cache/clock"
};

/* ========================================================================
 * src/cache/fifo.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/fifo/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * FIFO — evict the key that arrived first.
 *
 * Mirrors index.ts and policy.py. A circular buffer of keys and two indices;
 * hits do nothing at all.
 */




typedef struct pb_cache_fifo_state {
    const pb_allocator *allocator;
    uint64_t *slots; /* resident keys in arrival order */
    uint32_t capacity;
    uint32_t slot_count; /* capacity + 1: a caller inserts before evicting */
    uint32_t head;
    uint32_t length;
} pb_cache_fifo_state;

static pb_cache *fifo_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_fifo_params *config = (const pb_cache_fifo_params *)params;
    pb_cache_fifo_state *self;
    uint32_t capacity;
    uint32_t slot_count;

    (void)rng; /* FIFO makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    if (capacity == 0u || capacity == UINT32_MAX) {
        return NULL;
    }
    slot_count = capacity + 1u;

    self = (pb_cache_fifo_state *)pb_alloc(allocator, sizeof(pb_cache_fifo_state));
    if (self == NULL) {
        return NULL;
    }

    self->slots = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    if (self->slots == NULL) {
        pb_free(allocator, self, sizeof(pb_cache_fifo_state));
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    self->head = 0;
    self->length = 0;
    return (pb_cache *)self;
}

static void fifo_destroy(pb_cache *cache)
{
    pb_cache_fifo_state *self = (pb_cache_fifo_state *)cache;
    const pb_allocator *allocator;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    pb_free(allocator, self->slots, (size_t)self->slot_count * sizeof(uint64_t));
    pb_free(allocator, self, sizeof(pb_cache_fifo_state));
}

static void fifo_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_fifo_state *self = (pb_cache_fifo_state *)cache;
    uint32_t index;

    (void)meta;
    assert(self != NULL);

    /* The whole policy: a hit changes nothing. FIFO does not learn. */
    if (hit) {
        return;
    }

    /* The caller must evict once over capacity; ignoring that would silently
     * overwrite a live entry. */
    assert(self->length < self->slot_count);
    if (self->length >= self->slot_count) {
        return;
    }

    index = self->head + self->length;
    if (index >= self->slot_count) {
        index -= self->slot_count;
    }
    self->slots[index] = key;
    self->length += 1u;
}

static uint64_t fifo_evict(pb_cache *cache)
{
    pb_cache_fifo_state *self = (pb_cache_fifo_state *)cache;
    uint64_t key;

    assert(self != NULL);
    assert(self->length > 0u);
    if (self->length == 0u) {
        return 0u;
    }

    key = self->slots[self->head];
    self->head += 1u;
    if (self->head >= self->slot_count) {
        self->head = 0;
    }
    self->length -= 1u;
    return key;
}

static size_t fifo_memory_bytes(const pb_cache *cache)
{
    const pb_cache_fifo_state *self = (const pb_cache_fifo_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_fifo_state) + (size_t)self->slot_count * sizeof(uint64_t);
}

const pb_cache_vtable pb_cache_fifo = {
    fifo_create,
    fifo_on_access,
    fifo_evict,
    NULL, /* admits everything */
    fifo_destroy,
    fifo_memory_bytes,
    false, /* allocates_after_create */
    "cache/fifo"
};

/* ========================================================================
 * src/cache/lfu.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/lfu/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * LFU — evict the key used least often.
 *
 * Mirrors index.ts and policy.py, including the O(1) frequency-class
 * construction. Everything lives in index arrays taken in create; nothing
 * allocates afterwards, and no operation scans.
 */




#define PB_LFU_NIL 0xFFFFFFFFu

typedef struct pb_cache_lfu_state {
    const pb_allocator *allocator;
    uint32_t capacity;
    uint32_t slot_count; /* capacity + 1: a caller inserts before evicting */

    pb_map index; /* key -> entry slot */
    uint64_t *keys;

    /* Entries, linked within their frequency class. */
    uint32_t *entry_next;
    uint32_t *entry_prev;
    uint32_t *entry_class;
    uint32_t *free_entries;
    uint32_t free_entry_count;

    /* Frequency classes, linked in ascending order of frequency. */
    uint32_t *class_freq;
    uint32_t *class_head;
    uint32_t *class_tail;
    uint32_t *class_next;
    uint32_t *class_prev;
    uint32_t *free_classes;
    uint32_t free_class_count;
    uint32_t class_list_head; /* the lowest frequency: the eviction candidates */
} pb_cache_lfu_state;

/* Ten uint32 arrays of slot_count, plus the key array. */
#define PB_LFU_U32_ARRAYS 10u

static void lfu_release(pb_cache_lfu_state *self)
{
    const pb_allocator *allocator = self->allocator;
    size_t keys_bytes = (size_t)self->slot_count * sizeof(uint64_t);
    size_t u32_bytes = (size_t)self->slot_count * sizeof(uint32_t);

    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self->keys, keys_bytes);
    pb_free(allocator, self->entry_next, u32_bytes);
    pb_free(allocator, self->entry_prev, u32_bytes);
    pb_free(allocator, self->entry_class, u32_bytes);
    pb_free(allocator, self->free_entries, u32_bytes);
    pb_free(allocator, self->class_freq, u32_bytes);
    pb_free(allocator, self->class_head, u32_bytes);
    pb_free(allocator, self->class_tail, u32_bytes);
    pb_free(allocator, self->class_next, u32_bytes);
    pb_free(allocator, self->class_prev, u32_bytes);
    pb_free(allocator, self->free_classes, u32_bytes);
    pb_free(allocator, self, sizeof(pb_cache_lfu_state));
}

static pb_cache *lfu_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_lfu_params *config = (const pb_cache_lfu_params *)params;
    pb_cache_lfu_state *self;
    uint32_t capacity;
    uint32_t slot_count;
    size_t u32_bytes;
    uint32_t slot;
    bool ok;

    (void)rng; /* LFU makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    if (capacity == 0u || capacity == UINT32_MAX) {
        return NULL;
    }
    slot_count = capacity + 1u;
    u32_bytes = (size_t)slot_count * sizeof(uint32_t);

    self = (pb_cache_lfu_state *)pb_alloc(allocator, sizeof(pb_cache_lfu_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    self->index.capacity = 0;
    self->keys = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    self->entry_next = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->entry_prev = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->entry_class = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->free_entries = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->class_freq = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->class_head = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->class_tail = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->class_next = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->class_prev = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->free_classes = (uint32_t *)pb_alloc(allocator, u32_bytes);

    ok = self->keys != NULL && self->entry_next != NULL && self->entry_prev != NULL &&
         self->entry_class != NULL && self->free_entries != NULL && self->class_freq != NULL &&
         self->class_head != NULL && self->class_tail != NULL && self->class_next != NULL &&
         self->class_prev != NULL && self->free_classes != NULL &&
         pb_map_init(&self->index, slot_count, allocator);
    if (!ok) {
        lfu_release(self);
        return NULL;
    }

    for (slot = 0; slot < slot_count; ++slot) {
        self->entry_next[slot] = PB_LFU_NIL;
        self->entry_prev[slot] = PB_LFU_NIL;
        self->entry_class[slot] = PB_LFU_NIL;
        self->free_entries[slot] = slot_count - 1u - slot;
        self->class_head[slot] = PB_LFU_NIL;
        self->class_tail[slot] = PB_LFU_NIL;
        self->class_next[slot] = PB_LFU_NIL;
        self->class_prev[slot] = PB_LFU_NIL;
        self->class_freq[slot] = 0;
        self->free_classes[slot] = slot_count - 1u - slot;
    }
    self->free_entry_count = slot_count;
    self->free_class_count = slot_count;
    self->class_list_head = PB_LFU_NIL;

    return (pb_cache *)self;
}

static void lfu_destroy(pb_cache *cache)
{
    pb_cache_lfu_state *self = (pb_cache_lfu_state *)cache;

    if (self == NULL) {
        return;
    }
    lfu_release(self);
}

static uint32_t lfu_allocate_class(pb_cache_lfu_state *self, uint32_t frequency)
{
    uint32_t klass;

    assert(self->free_class_count > 0u);
    self->free_class_count -= 1u;
    klass = self->free_classes[self->free_class_count];
    self->class_freq[klass] = frequency;
    self->class_head[klass] = PB_LFU_NIL;
    self->class_tail[klass] = PB_LFU_NIL;
    return klass;
}

/* Create a class at the front of the ascending list. */
static uint32_t lfu_insert_class_front(pb_cache_lfu_state *self, uint32_t frequency)
{
    uint32_t klass = lfu_allocate_class(self, frequency);

    self->class_prev[klass] = PB_LFU_NIL;
    self->class_next[klass] = self->class_list_head;
    if (self->class_list_head != PB_LFU_NIL) {
        self->class_prev[self->class_list_head] = klass;
    }
    self->class_list_head = klass;
    return klass;
}

static uint32_t lfu_insert_class_after(pb_cache_lfu_state *self, uint32_t after,
                                       uint32_t frequency)
{
    uint32_t klass = lfu_allocate_class(self, frequency);
    uint32_t following = self->class_next[after];

    self->class_prev[klass] = after;
    self->class_next[klass] = following;
    self->class_next[after] = klass;
    if (following != PB_LFU_NIL) {
        self->class_prev[following] = klass;
    }
    return klass;
}

static void lfu_release_class(pb_cache_lfu_state *self, uint32_t klass)
{
    uint32_t following = self->class_next[klass];
    uint32_t preceding = self->class_prev[klass];

    if (preceding != PB_LFU_NIL) {
        self->class_next[preceding] = following;
    } else {
        self->class_list_head = following;
    }
    if (following != PB_LFU_NIL) {
        self->class_prev[following] = preceding;
    }

    self->class_next[klass] = PB_LFU_NIL;
    self->class_prev[klass] = PB_LFU_NIL;
    self->free_classes[self->free_class_count] = klass;
    self->free_class_count += 1u;
}

static void lfu_append_entry(pb_cache_lfu_state *self, uint32_t klass, uint32_t entry)
{
    uint32_t tail = self->class_tail[klass];

    self->entry_class[entry] = klass;
    self->entry_next[entry] = PB_LFU_NIL;
    self->entry_prev[entry] = tail;

    if (tail != PB_LFU_NIL) {
        self->entry_next[tail] = entry;
    } else {
        self->class_head[klass] = entry;
    }
    self->class_tail[klass] = entry;
}

static void lfu_remove_entry(pb_cache_lfu_state *self, uint32_t klass, uint32_t entry)
{
    uint32_t following = self->entry_next[entry];
    uint32_t preceding = self->entry_prev[entry];

    if (preceding != PB_LFU_NIL) {
        self->entry_next[preceding] = following;
    } else {
        self->class_head[klass] = following;
    }
    if (following != PB_LFU_NIL) {
        self->entry_prev[following] = preceding;
    } else {
        self->class_tail[klass] = preceding;
    }

    self->entry_next[entry] = PB_LFU_NIL;
    self->entry_prev[entry] = PB_LFU_NIL;
    self->entry_class[entry] = PB_LFU_NIL;

    /* An empty class carries no information and must not stay in the list. */
    if (self->class_head[klass] == PB_LFU_NIL) {
        lfu_release_class(self, klass);
    }
}

static void lfu_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_lfu_state *self = (pb_cache_lfu_state *)cache;
    uint32_t entry;
    uint32_t target;

    (void)meta;
    assert(self != NULL);

    if (hit) {
        uint32_t from;
        uint32_t frequency;
        uint32_t following;

        if (!pb_map_get(&self->index, key, &entry)) {
            assert(false); /* the caller's residency tracking is wrong */
            return;
        }

        from = self->entry_class[entry];
        frequency = self->class_freq[from];
        following = self->class_next[from];

        /* Reuse the neighbouring class if it is already the frequency we want. */
        if (following != PB_LFU_NIL && self->class_freq[following] == frequency + 1u) {
            target = following;
        } else {
            target = lfu_insert_class_after(self, from, frequency + 1u);
        }

        lfu_remove_entry(self, from, entry);
        lfu_append_entry(self, target, entry);
        return;
    }

    assert(self->free_entry_count > 0u);
    if (self->free_entry_count == 0u) {
        return;
    }

    self->free_entry_count -= 1u;
    entry = self->free_entries[self->free_entry_count];
    self->keys[entry] = key;
    (void)pb_map_put(&self->index, key, entry);

    /* A new entry has frequency 1, so it belongs at the front of the list. */
    if (self->class_list_head != PB_LFU_NIL && self->class_freq[self->class_list_head] == 1u) {
        target = self->class_list_head;
    } else {
        target = lfu_insert_class_front(self, 1u);
    }
    lfu_append_entry(self, target, entry);
}

static uint64_t lfu_evict(pb_cache *cache)
{
    pb_cache_lfu_state *self = (pb_cache_lfu_state *)cache;
    uint32_t klass;
    uint32_t entry;
    uint64_t key;

    assert(self != NULL);
    assert(self->class_list_head != PB_LFU_NIL);
    if (self->class_list_head == PB_LFU_NIL) {
        return 0u;
    }

    /* The first class holds the lowest frequency; its head reached that
     * frequency earliest, which is the documented tie-break. */
    klass = self->class_list_head;
    entry = self->class_head[klass];
    key = self->keys[entry];

    lfu_remove_entry(self, klass, entry);
    (void)pb_map_remove(&self->index, key);
    self->free_entries[self->free_entry_count] = entry;
    self->free_entry_count += 1u;

    return key;
}

static size_t lfu_memory_bytes(const pb_cache *cache)
{
    const pb_cache_lfu_state *self = (const pb_cache_lfu_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_lfu_state) + (size_t)self->slot_count * sizeof(uint64_t) +
           (size_t)self->slot_count * PB_LFU_U32_ARRAYS * sizeof(uint32_t) +
           pb_map_memory_bytes(&self->index);
}

const pb_cache_vtable pb_cache_lfu = {
    lfu_create,
    lfu_on_access,
    lfu_evict,
    NULL, /* admits everything */
    lfu_destroy,
    lfu_memory_bytes,
    false, /* allocates_after_create */
    "cache/lfu"
};

/* ========================================================================
 * src/cache/lru.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/lru/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * LRU — evict the key used longest ago.
 *
 * Mirrors index.ts and policy.py: a map from key to slot, and an index-based
 * doubly linked list giving recency order. Slots are recycled through a free
 * stack, so nothing allocates after create.
 */




typedef struct pb_cache_lru_state {
    const pb_allocator *allocator;
    uint32_t capacity;
    uint32_t slot_count; /* capacity + 1: a caller inserts before evicting */

    pb_map index;    /* key -> slot */
    pb_ilist recency; /* head is most recently used, tail is the victim */
    uint64_t *keys;  /* slot -> key */

    uint32_t *free_slots;
    uint32_t free_count;
} pb_cache_lru_state;

static pb_cache *lru_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_lru_params *config = (const pb_cache_lru_params *)params;
    pb_cache_lru_state *self;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t slot;

    (void)rng; /* LRU makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    if (capacity == 0u || capacity == UINT32_MAX) {
        return NULL;
    }
    slot_count = capacity + 1u;

    self = (pb_cache_lru_state *)pb_alloc(allocator, sizeof(pb_cache_lru_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    self->free_count = 0;
    self->keys = NULL;
    self->free_slots = NULL;
    self->index.capacity = 0;
    self->recency.capacity = 0;

    self->keys = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    self->free_slots = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    if (self->keys == NULL || self->free_slots == NULL ||
        !pb_map_init(&self->index, slot_count, allocator) ||
        !pb_ilist_init(&self->recency, slot_count, allocator)) {
        /* destroy tolerates partially built state */
        pb_map_destroy(&self->index, allocator);
        pb_ilist_destroy(&self->recency, allocator);
        pb_free(allocator, self->keys, (size_t)slot_count * sizeof(uint64_t));
        pb_free(allocator, self->free_slots, (size_t)slot_count * sizeof(uint32_t));
        pb_free(allocator, self, sizeof(pb_cache_lru_state));
        return NULL;
    }

    for (slot = 0; slot < slot_count; ++slot) {
        self->free_slots[slot] = slot_count - 1u - slot;
    }
    self->free_count = slot_count;

    return (pb_cache *)self;
}

static void lru_destroy(pb_cache *cache)
{
    pb_cache_lru_state *self = (pb_cache_lru_state *)cache;
    const pb_allocator *allocator;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    pb_map_destroy(&self->index, allocator);
    pb_ilist_destroy(&self->recency, allocator);
    pb_free(allocator, self->keys, (size_t)self->slot_count * sizeof(uint64_t));
    pb_free(allocator, self->free_slots, (size_t)self->slot_count * sizeof(uint32_t));
    pb_free(allocator, self, sizeof(pb_cache_lru_state));
}

static void lru_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_lru_state *self = (pb_cache_lru_state *)cache;
    uint32_t slot;

    (void)meta;
    assert(self != NULL);

    if (hit) {
        /* A caller reporting a hit for an absent key has a residency bug. */
        if (!pb_map_get(&self->index, key, &slot)) {
            assert(false);
            return;
        }
        pb_ilist_move_to_front(&self->recency, slot);
        return;
    }

    assert(self->free_count > 0u);
    if (self->free_count == 0u) {
        return;
    }

    self->free_count -= 1u;
    slot = self->free_slots[self->free_count];
    self->keys[slot] = key;
    (void)pb_map_put(&self->index, key, slot);
    pb_ilist_push_front(&self->recency, slot);
}

static uint64_t lru_evict(pb_cache *cache)
{
    pb_cache_lru_state *self = (pb_cache_lru_state *)cache;
    uint32_t slot;
    uint64_t key;

    assert(self != NULL);

    slot = pb_ilist_pop_back(&self->recency);
    assert(slot != PB_ILIST_NIL);
    if (slot == PB_ILIST_NIL) {
        return 0u;
    }

    key = self->keys[slot];
    (void)pb_map_remove(&self->index, key);
    self->free_slots[self->free_count] = slot;
    self->free_count += 1u;
    return key;
}

static size_t lru_memory_bytes(const pb_cache *cache)
{
    const pb_cache_lru_state *self = (const pb_cache_lru_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_lru_state) +
           (size_t)self->slot_count * (sizeof(uint64_t) + sizeof(uint32_t)) +
           pb_map_memory_bytes(&self->index) + pb_ilist_memory_bytes(&self->recency);
}

const pb_cache_vtable pb_cache_lru = {
    lru_create,
    lru_on_access,
    lru_evict,
    NULL, /* admits everything */
    lru_destroy,
    lru_memory_bytes,
    false, /* allocates_after_create */
    "cache/lru"
};

/* ========================================================================
 * src/cache/s3_fifo.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/s3-fifo/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * S3-FIFO — three FIFO queues, two bits per entry, no list surgery.
 *
 * Mirrors index.ts and policy.py, including the one adaptation from the paper
 * documented in README.md: eviction returns exactly one victim per call.
 */




/* Two bits per entry: three second chances and no more. */
#define PB_S3_MAX_FREQUENCY 3u
/* An entry must have been requested more than once in S to be promoted. */
#define PB_S3_PROMOTION_THRESHOLD 1u

#define PB_S3_SMALL 0u
#define PB_S3_MAIN 1u

typedef struct pb_cache_s3_fifo_state {
    const pb_allocator *allocator;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t small_size;
    uint32_t main_size;

    pb_map index; /* resident key -> slot */
    uint64_t *keys;
    uint8_t *frequency;
    uint8_t *queue_of;

    pb_ring small;
    pb_ring main;

    /* G: keys of objects that fell out of S, newest at the list head. A list,
     * not a ring, because promotion removes a ghost from the middle and the
     * remaining ghosts keep their order and their claim on G's capacity. */
    uint64_t *ghost_keys;   /* ghost slot -> key */
    pb_ilist ghost_list;    /* order; the tail is the oldest ghost */
    pb_map ghost_index;     /* ghost key -> its slot */
    uint32_t *ghost_free;   /* unused ghost slots */
    uint32_t ghost_free_count;

    uint32_t *free_slots;
    uint32_t free_count;
} pb_cache_s3_fifo_state;

static void pb_s3_release(pb_cache_s3_fifo_state *self)
{
    const pb_allocator *allocator = self->allocator;
    size_t slots = (size_t)self->slot_count;

    pb_map_destroy(&self->index, allocator);
    pb_map_destroy(&self->ghost_index, allocator);
    pb_ring_destroy(&self->small, allocator);
    pb_ring_destroy(&self->main, allocator);
    pb_ilist_destroy(&self->ghost_list, allocator);
    pb_free(allocator, self->keys, slots * sizeof(uint64_t));
    pb_free(allocator, self->frequency, slots * sizeof(uint8_t));
    pb_free(allocator, self->queue_of, slots * sizeof(uint8_t));
    pb_free(allocator, self->free_slots, slots * sizeof(uint32_t));
    pb_free(allocator, self->ghost_keys, (size_t)self->main_size * sizeof(uint64_t));
    pb_free(allocator, self->ghost_free, (size_t)self->main_size * sizeof(uint32_t));
    pb_free(allocator, self, sizeof(pb_cache_s3_fifo_state));
}

static pb_cache *s3_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_s3_fifo_params *config = (const pb_cache_s3_fifo_params *)params;
    pb_cache_s3_fifo_state *self;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t slot;
    double small_fraction;

    (void)rng; /* S3-FIFO makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    small_fraction = (config == NULL) ? 0.1 : config->small_fraction;

    /* Capacity 1 has no room for both a small and a main queue. */
    if (capacity < 2u || capacity == UINT32_MAX) {
        return NULL;
    }
    if (!(small_fraction > 0.0) || small_fraction >= 1.0) {
        return NULL;
    }

    slot_count = capacity + 1u;

    self = (pb_cache_s3_fifo_state *)pb_alloc(allocator, sizeof(pb_cache_s3_fifo_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    self->index.capacity = 0;
    self->ghost_index.capacity = 0;
    self->small.capacity = 0;
    self->main.capacity = 0;
    self->ghost_list.capacity = 0;
    self->ghost_free_count = 0;
    self->free_count = 0;

    self->small_size = (uint32_t)((double)capacity * small_fraction);
    if (self->small_size == 0u) {
        self->small_size = 1u;
    }
    self->main_size = capacity - self->small_size;

    self->keys = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    self->frequency = (uint8_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint8_t));
    self->queue_of = (uint8_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint8_t));
    self->free_slots = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->ghost_keys =
        (uint64_t *)pb_alloc(allocator, (size_t)self->main_size * sizeof(uint64_t));
    self->ghost_free =
        (uint32_t *)pb_alloc(allocator, (size_t)self->main_size * sizeof(uint32_t));

    if (self->keys == NULL || self->frequency == NULL || self->queue_of == NULL ||
        self->free_slots == NULL || self->ghost_keys == NULL || self->ghost_free == NULL ||
        !pb_map_init(&self->index, slot_count, allocator) ||
        !pb_map_init(&self->ghost_index, self->main_size, allocator) ||
        !pb_ring_init(&self->small, slot_count, allocator) ||
        !pb_ring_init(&self->main, slot_count, allocator) ||
        !pb_ilist_init(&self->ghost_list, self->main_size, allocator)) {
        pb_s3_release(self);
        return NULL;
    }

    for (slot = 0; slot < slot_count; ++slot) {
        self->frequency[slot] = 0u;
        self->queue_of[slot] = (uint8_t)PB_S3_SMALL;
        self->free_slots[slot] = slot_count - 1u - slot;
    }
    self->free_count = slot_count;
    for (slot = 0; slot < self->main_size; ++slot) {
        self->ghost_free[slot] = self->main_size - 1u - slot;
    }
    self->ghost_free_count = self->main_size;

    return (pb_cache *)self;
}

static void s3_destroy(pb_cache *cache)
{
    pb_cache_s3_fifo_state *self = (pb_cache_s3_fifo_state *)cache;

    if (self == NULL) {
        return;
    }
    pb_s3_release(self);
}

static void pb_s3_remember_ghost(pb_cache_s3_fifo_state *self, uint64_t key)
{
    uint32_t ghost_slot;

    if (self->ghost_list.length == self->main_size) {
        /* Full: the oldest identifier is forgotten. */
        ghost_slot = pb_ilist_pop_back(&self->ghost_list);
        (void)pb_map_remove(&self->ghost_index, self->ghost_keys[ghost_slot]);
    } else {
        self->ghost_free_count -= 1u;
        ghost_slot = self->ghost_free[self->ghost_free_count];
    }

    self->ghost_keys[ghost_slot] = key;
    pb_ilist_push_front(&self->ghost_list, ghost_slot);
    (void)pb_map_put(&self->ghost_index, key, ghost_slot);
}

static uint64_t pb_s3_release_slot(pb_cache_s3_fifo_state *self, uint32_t slot)
{
    uint64_t key = self->keys[slot];

    (void)pb_map_remove(&self->index, key);
    self->frequency[slot] = 0u;
    self->free_slots[self->free_count] = slot;
    self->free_count += 1u;
    return key;
}

static void s3_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_s3_fifo_state *self = (pb_cache_s3_fifo_state *)cache;
    uint32_t slot;
    uint32_t ghost_slot;

    (void)meta;
    assert(self != NULL);

    if (hit) {
        if (!pb_map_get(&self->index, key, &slot)) {
            assert(false); /* the caller's residency tracking is wrong */
            return;
        }
        /* The entire hit path: bump a two-bit counter. Nothing moves. */
        if (self->frequency[slot] < PB_S3_MAX_FREQUENCY) {
            self->frequency[slot] = (uint8_t)(self->frequency[slot] + 1u);
        }
        return;
    }

    assert(self->free_count > 0u);
    if (self->free_count == 0u) {
        return;
    }

    self->free_count -= 1u;
    slot = self->free_slots[self->free_count];
    self->keys[slot] = key;
    self->frequency[slot] = 0u;
    (void)pb_map_put(&self->index, key, slot);

    if (pb_map_get(&self->ghost_index, key, &ghost_slot)) {
        /* Falling out of the small queue and coming back is itself evidence of
         * reuse, so the key skips the audition. Promotion removes exactly this
         * ghost; the others keep their order and their claim on G's capacity. */
        (void)pb_map_remove(&self->ghost_index, key);
        pb_ilist_remove(&self->ghost_list, ghost_slot);
        self->ghost_free[self->ghost_free_count] = ghost_slot;
        self->ghost_free_count += 1u;
        self->queue_of[slot] = (uint8_t)PB_S3_MAIN;
        (void)pb_ring_push_back(&self->main, slot);
        return;
    }

    self->queue_of[slot] = (uint8_t)PB_S3_SMALL;
    (void)pb_ring_push_back(&self->small, slot);
}

/*
 * Drain the small queue until something leaves the cache.
 *
 * Returns true and sets `victim` when an entry was evicted; false if the small
 * queue emptied by promotion without evicting anything.
 */
static bool pb_s3_evict_from_small(pb_cache_s3_fifo_state *self, uint64_t *victim)
{
    while (!pb_ring_is_empty(&self->small)) {
        uint32_t slot = pb_ring_pop_front(&self->small);

        if (self->frequency[slot] > PB_S3_PROMOTION_THRESHOLD) {
            self->queue_of[slot] = (uint8_t)PB_S3_MAIN;
            (void)pb_ring_push_back(&self->main, slot);
            continue;
        }

        pb_s3_remember_ghost(self, self->keys[slot]);
        *victim = pb_s3_release_slot(self, slot);
        return true;
    }
    return false;
}

/*
 * Take from the main queue, spending each entry's remaining second chances.
 *
 * Terminates because every pass decrements a counter that a later pass cannot
 * spend again.
 */
static bool pb_s3_evict_from_main(pb_cache_s3_fifo_state *self, uint64_t *victim)
{
    while (!pb_ring_is_empty(&self->main)) {
        uint32_t slot = pb_ring_pop_front(&self->main);

        if (self->frequency[slot] > 0u) {
            self->frequency[slot] = (uint8_t)(self->frequency[slot] - 1u);
            (void)pb_ring_push_back(&self->main, slot);
            continue;
        }

        *victim = pb_s3_release_slot(self, slot);
        return true;
    }
    return false;
}

static uint64_t s3_evict(pb_cache *cache)
{
    pb_cache_s3_fifo_state *self = (pb_cache_s3_fifo_state *)cache;
    uint64_t victim = 0u;

    assert(self != NULL);

    /* The small queue is drained while it is over its share, so an object that
     * has not proven reuse is always the better victim. */
    if (self->small.length >= self->small_size && pb_s3_evict_from_small(self, &victim)) {
        return victim;
    }
    if (pb_s3_evict_from_main(self, &victim)) {
        return victim;
    }
    /* The main queue is empty; fall back to the small one however short. */
    if (pb_s3_evict_from_small(self, &victim)) {
        return victim;
    }

    assert(false); /* nothing resident */
    return 0u;
}

static size_t s3_memory_bytes(const pb_cache *cache)
{
    const pb_cache_s3_fifo_state *self = (const pb_cache_s3_fifo_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_s3_fifo_state) +
           (size_t)self->slot_count *
               (sizeof(uint64_t) + 2u * sizeof(uint8_t) + sizeof(uint32_t)) +
           (size_t)self->main_size * (sizeof(uint64_t) + sizeof(uint32_t)) +
           pb_map_memory_bytes(&self->index) + pb_map_memory_bytes(&self->ghost_index) +
           pb_ring_memory_bytes(&self->small) + pb_ring_memory_bytes(&self->main) +
           pb_ilist_memory_bytes(&self->ghost_list);
}

const pb_cache_vtable pb_cache_s3_fifo = {
    s3_create,
    s3_on_access,
    s3_evict,
    NULL, /* admission is decided by the small queue, not at insertion */
    s3_destroy,
    s3_memory_bytes,
    false, /* allocates_after_create */
    "cache/s3-fifo"
};

/* ========================================================================
 * src/cache/sieve.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/sieve/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * SIEVE — a FIFO queue with a hand that gives each entry one chance.
 *
 * Mirrors index.ts and policy.py. Entries are held in an index-based doubly
 * linked list because the hand removes from the middle; `newer` and `older`
 * name the directions, which the paper's prev/next leave ambiguous when the
 * hand travels one way and insertion happens at the other end.
 */




#define PB_SIEVE_NIL 0xFFFFFFFFu

typedef struct pb_cache_sieve_state {
    const pb_allocator *allocator;
    uint32_t capacity;
    uint32_t slot_count; /* capacity + 1: a caller inserts before evicting */

    pb_map index;   /* key -> slot */
    uint64_t *keys; /* slot -> key */
    uint8_t *visited;

    /* Insertion order. Nothing ever moves within it. */
    uint32_t *newer;
    uint32_t *older;
    uint32_t newest;
    uint32_t oldest;

    /*
     * Where the hand stopped, or PB_SIEVE_NIL to start from the oldest entry.
     * Retaining this across evictions is the algorithm, not an optimisation.
     */
    uint32_t hand;

    uint32_t *free_slots;
    uint32_t free_count;
} pb_cache_sieve_state;

static void sieve_release(pb_cache_sieve_state *self)
{
    const pb_allocator *allocator = self->allocator;
    size_t slots = (size_t)self->slot_count;

    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self->keys, slots * sizeof(uint64_t));
    pb_free(allocator, self->visited, slots * sizeof(uint8_t));
    pb_free(allocator, self->newer, slots * sizeof(uint32_t));
    pb_free(allocator, self->older, slots * sizeof(uint32_t));
    pb_free(allocator, self->free_slots, slots * sizeof(uint32_t));
    pb_free(allocator, self, sizeof(pb_cache_sieve_state));
}

static pb_cache *sieve_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_sieve_params *config = (const pb_cache_sieve_params *)params;
    pb_cache_sieve_state *self;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t slot;

    (void)rng; /* SIEVE makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    if (capacity == 0u || capacity == UINT32_MAX) {
        return NULL;
    }
    slot_count = capacity + 1u;

    self = (pb_cache_sieve_state *)pb_alloc(allocator, sizeof(pb_cache_sieve_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    self->free_count = 0;
    self->index.capacity = 0;
    self->keys = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    self->visited = (uint8_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint8_t));
    self->newer = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->older = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->free_slots = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));

    if (self->keys == NULL || self->visited == NULL || self->newer == NULL ||
        self->older == NULL || self->free_slots == NULL ||
        !pb_map_init(&self->index, slot_count, allocator)) {
        sieve_release(self);
        return NULL;
    }

    for (slot = 0; slot < slot_count; ++slot) {
        self->visited[slot] = 0u;
        self->newer[slot] = PB_SIEVE_NIL;
        self->older[slot] = PB_SIEVE_NIL;
        self->free_slots[slot] = slot_count - 1u - slot;
    }
    self->free_count = slot_count;
    self->newest = PB_SIEVE_NIL;
    self->oldest = PB_SIEVE_NIL;
    self->hand = PB_SIEVE_NIL;

    return (pb_cache *)self;
}

static void sieve_destroy(pb_cache *cache)
{
    pb_cache_sieve_state *self = (pb_cache_sieve_state *)cache;

    if (self == NULL) {
        return;
    }
    sieve_release(self);
}

static void sieve_unlink(pb_cache_sieve_state *self, uint32_t slot)
{
    uint32_t newer = self->newer[slot];
    uint32_t older = self->older[slot];

    if (newer != PB_SIEVE_NIL) {
        self->older[newer] = older;
    } else {
        self->newest = older;
    }
    if (older != PB_SIEVE_NIL) {
        self->newer[older] = newer;
    } else {
        self->oldest = newer;
    }

    self->newer[slot] = PB_SIEVE_NIL;
    self->older[slot] = PB_SIEVE_NIL;
}

static void sieve_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_sieve_state *self = (pb_cache_sieve_state *)cache;
    uint32_t slot;

    (void)meta;
    assert(self != NULL);

    if (hit) {
        if (!pb_map_get(&self->index, key, &slot)) {
            assert(false); /* the caller's residency tracking is wrong */
            return;
        }
        /* The entire hit path: set one bit. The entry does not move. */
        self->visited[slot] = 1u;
        return;
    }

    assert(self->free_count > 0u);
    if (self->free_count == 0u) {
        return;
    }

    self->free_count -= 1u;
    slot = self->free_slots[self->free_count];
    self->keys[slot] = key;
    self->visited[slot] = 0u;
    (void)pb_map_put(&self->index, key, slot);

    /* New entries go at the new end, ahead of the hand. */
    self->newer[slot] = PB_SIEVE_NIL;
    self->older[slot] = self->newest;
    if (self->newest != PB_SIEVE_NIL) {
        self->newer[self->newest] = slot;
    } else {
        self->oldest = slot;
    }
    self->newest = slot;
}

static uint64_t sieve_evict(pb_cache *cache)
{
    pb_cache_sieve_state *self = (pb_cache_sieve_state *)cache;
    uint32_t slot;
    uint64_t key;

    assert(self != NULL);
    assert(self->oldest != PB_SIEVE_NIL);
    if (self->oldest == PB_SIEVE_NIL) {
        return 0u;
    }

    /* Resume where the hand stopped, or start at the oldest entry. */
    slot = (self->hand == PB_SIEVE_NIL) ? self->oldest : self->hand;

    while (self->visited[slot] != 0u) {
        uint32_t next = self->newer[slot];
        self->visited[slot] = 0u;
        /* Past the newest entry, the hand wraps to the oldest. */
        slot = (next == PB_SIEVE_NIL) ? self->oldest : next;
    }

    /* The hand stops just beyond the victim, and stays there. */
    self->hand = self->newer[slot];

    key = self->keys[slot];
    sieve_unlink(self, slot);
    (void)pb_map_remove(&self->index, key);
    self->free_slots[self->free_count] = slot;
    self->free_count += 1u;
    return key;
}

static size_t sieve_memory_bytes(const pb_cache *cache)
{
    const pb_cache_sieve_state *self = (const pb_cache_sieve_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_sieve_state) +
           (size_t)self->slot_count *
               (sizeof(uint64_t) + sizeof(uint8_t) + 3u * sizeof(uint32_t)) +
           pb_map_memory_bytes(&self->index);
}

const pb_cache_vtable pb_cache_sieve = {
    sieve_create,
    sieve_on_access,
    sieve_evict,
    NULL, /* admits everything */
    sieve_destroy,
    sieve_memory_bytes,
    false, /* allocates_after_create */
    "cache/sieve"
};

/* ========================================================================
 * src/cache/traces.c
 * ======================================================================== */

/* How often the scan-heavy trace injects a scan. */
#define PB_SCAN_INTERVAL 20000u
/* How far the popular set rotates each time, in keys. */
#define PB_POPULARITY_SHIFT 2500u
/* How often the popular set rotates. */
#define PB_SHIFT_INTERVAL 25000u

const pb_cache_trace_spec pb_cache_traces[] = {
    { "zipf-1.0-100k", PB_CACHE_TRACE_ZIPF, 1.0, 10000u, 10000u, 1000u, 100000u, 42u },
    { "zipf-0.75-1m", PB_CACHE_TRACE_ZIPF, 0.75, 100000u, 100000u, 10000u, 1000000u, 43u },
    /* The scans emit keys above the Zipf keyspace: 10,000 .. 17,999. */
    { "scan-heavy", PB_CACHE_TRACE_SCAN_HEAVY, 1.0, 10000u, 18000u, 1000u, 108000u, 44u },
    { "shifting-popularity", PB_CACHE_TRACE_SHIFTING, 1.0, 10000u, 10000u, 1000u, 100000u, 45u }
};

const pb_cache_trace_spec *pb_cache_trace_find(const char *id)
{
    size_t i;

    if (id == NULL) {
        return NULL;
    }
    for (i = 0; i < (size_t)PB_CACHE_TRACE_COUNT; ++i) {
        if (strcmp(pb_cache_traces[i].id, id) == 0) {
            return &pb_cache_traces[i];
        }
    }
    return NULL;
}

/* Plain Zipf: draw a rank, emit it as the key. */
static size_t generate_zipf(const pb_cache_trace_spec *spec, uint32_t *out, size_t limit,
                            const pb_allocator *allocator)
{
    pb_rng rng;
    pb_zipf zipf;
    size_t index;

    pb_rng_init(&rng, spec->seed);
    if (!pb_zipf_init(&zipf, spec->keyspace, spec->alpha, allocator)) {
        return 0;
    }

    for (index = 0; index < limit; ++index) {
        out[index] = pb_zipf_sample(&zipf, &rng);
    }

    pb_zipf_destroy(&zipf, allocator);
    return limit;
}

/*
 * Zipf with periodic sequential scans.
 *
 * Every PB_SCAN_INTERVAL accesses, a run of 2 x capacity fresh keys sweeps
 * through — the shape of a backup, a table scan, or a crawler. LRU caches all of
 * it and throws away its working set; a scan-resistant policy barely notices.
 * Scan keys live above the Zipf keyspace so they can never collide with it.
 */
static size_t generate_scan_heavy(const pb_cache_trace_spec *spec, uint32_t *out, size_t limit,
                                  const pb_allocator *allocator)
{
    pb_rng rng;
    pb_zipf zipf;
    size_t written = 0;
    uint32_t scan_index = 0;
    uint32_t scan_length = spec->capacity * 2u;
    uint32_t step;

    pb_rng_init(&rng, spec->seed);
    if (!pb_zipf_init(&zipf, spec->keyspace, spec->alpha, allocator)) {
        return 0;
    }

    for (step = 0; step < spec->events && written < limit; ++step) {
        if (step > 0u && step % PB_SCAN_INTERVAL == 0u) {
            uint32_t scan_base = spec->keyspace + scan_index * scan_length;
            uint32_t offset;
            for (offset = 0; offset < scan_length && written < limit; ++offset) {
                out[written] = scan_base + offset;
                written += 1u;
            }
            scan_index += 1u;
            if (written >= limit) {
                break;
            }
        }

        out[written] = pb_zipf_sample(&zipf, &rng);
        written += 1u;
    }

    pb_zipf_destroy(&zipf, allocator);
    return written;
}

/*
 * Zipf whose popular set rotates.
 *
 * The rank drawn is the same as ever, but it is offset by a rotation that
 * advances every PB_SHIFT_INTERVAL accesses, so yesterday's hot keys go cold.
 * Frequency-based policies that never forget keep the wrong entries.
 */
static size_t generate_shifting(const pb_cache_trace_spec *spec, uint32_t *out, size_t limit,
                                const pb_allocator *allocator)
{
    pb_rng rng;
    pb_zipf zipf;
    size_t step;

    pb_rng_init(&rng, spec->seed);
    if (!pb_zipf_init(&zipf, spec->keyspace, spec->alpha, allocator)) {
        return 0;
    }

    for (step = 0; step < limit; ++step) {
        uint32_t rank = pb_zipf_sample(&zipf, &rng);
        uint32_t shift = (uint32_t)(step / PB_SHIFT_INTERVAL) * PB_POPULARITY_SHIFT;
        out[step] = (rank + shift) % spec->keyspace;
    }

    pb_zipf_destroy(&zipf, allocator);
    return limit;
}

size_t pb_cache_trace_generate(const pb_cache_trace_spec *spec, uint32_t *out, size_t max_events,
                               const pb_allocator *allocator)
{
    size_t limit;

    assert(spec != NULL);
    assert(out != NULL);

    limit = max_events;
    if (limit > (size_t)spec->events) {
        limit = (size_t)spec->events;
    }
    if (limit == 0u) {
        return 0;
    }

    switch (spec->kind) {
    case PB_CACHE_TRACE_ZIPF:
        return generate_zipf(spec, out, limit, allocator);
    case PB_CACHE_TRACE_SCAN_HEAVY:
        return generate_scan_heavy(spec, out, limit, allocator);
    case PB_CACHE_TRACE_SHIFTING:
        return generate_shifting(spec, out, limit, allocator);
    default:
        return 0;
    }
}

/* ========================================================================
 * src/cache/w_tinylfu.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/cache/w-tinylfu/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * W-TinyLFU — frequency-based admission on four bits per counter.
 *
 * Mirrors index.ts and policy.py, including every sketch parameter: the three
 * implementations must produce identical estimates from identical accesses.
 */




#define PB_WTLFU_NIL 0xFFFFFFFFu

/* Segments, used to index the head/tail/length arrays. */
#define PB_WTLFU_WINDOW 0u
#define PB_WTLFU_PROBATION 1u
#define PB_WTLFU_PROTECTED 2u
#define PB_WTLFU_SEGMENTS 3u

#define PB_WTLFU_ROWS 4u
/* Counters saturate here; four bits cannot hold more. */
#define PB_WTLFU_MAX_COUNT 15u

/* Row salts for the sketch. Arbitrary odd constants with good bit mixing. */
static const uint32_t PB_WTLFU_SALT[PB_WTLFU_ROWS] = { 0x9E3779B9u, 0x85EBCA6Bu, 0xC2B2AE35u,
                                                       0x27D4EB2Fu };

typedef struct pb_cache_w_tinylfu_state {
    const pb_allocator *allocator;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t window_size;
    uint32_t main_size;
    uint32_t protected_size;

    pb_map index;
    uint64_t *keys;

    uint32_t *next;
    uint32_t *prev;
    uint8_t *segment_of;
    uint32_t heads[PB_WTLFU_SEGMENTS];
    uint32_t tails[PB_WTLFU_SEGMENTS];
    uint32_t lengths[PB_WTLFU_SEGMENTS];

    uint32_t *free_slots;
    uint32_t free_count;

    uint8_t *sketch; /* four-bit counters, two to a byte */
    uint32_t sketch_width;
    uint32_t sketch_mask;
    uint32_t sketch_bytes;

    uint8_t *doorkeeper;
    uint32_t doorkeeper_mask;
    uint32_t doorkeeper_bytes;

    uint32_t sampled;
    uint32_t sample_limit;
} pb_cache_w_tinylfu_state;

static uint64_t pb_wtlfu_next_power_of_two(uint64_t value)
{
    uint64_t result = 1u;

    while (result < value) {
        result *= 2u;
    }
    return result;
}

static void pb_wtlfu_release(pb_cache_w_tinylfu_state *self)
{
    const pb_allocator *allocator = self->allocator;
    size_t slots = (size_t)self->slot_count;

    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self->keys, slots * sizeof(uint64_t));
    pb_free(allocator, self->next, slots * sizeof(uint32_t));
    pb_free(allocator, self->prev, slots * sizeof(uint32_t));
    pb_free(allocator, self->segment_of, slots * sizeof(uint8_t));
    pb_free(allocator, self->free_slots, slots * sizeof(uint32_t));
    pb_free(allocator, self->sketch, (size_t)self->sketch_bytes);
    pb_free(allocator, self->doorkeeper, (size_t)self->doorkeeper_bytes);
    pb_free(allocator, self, sizeof(pb_cache_w_tinylfu_state));
}

static pb_cache *wtlfu_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_w_tinylfu_params *config = (const pb_cache_w_tinylfu_params *)params;
    pb_cache_w_tinylfu_state *self;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t slot;
    uint32_t segment;
    uint64_t sketch_width;
    double window_fraction;
    double protected_fraction;

    (void)rng; /* W-TinyLFU makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    window_fraction = (config == NULL) ? 0.01 : config->window_fraction;
    protected_fraction = (config == NULL) ? 0.8 : config->protected_fraction;

    /* Capacity 1 has no room for both a window and a main cache. The sketch
     * is eight positions per entry rounded up to a power of two, addressed as
     * row * width + slot in 32 bits: four rows of next_pow2(8 * capacity)
     * positions must stay within 2^32, which caps capacity at 2^27. */
    if (capacity < 2u || capacity > (UINT32_C(1) << 27)) {
        return NULL;
    }
    if (!(window_fraction > 0.0) || window_fraction >= 1.0) {
        return NULL;
    }
    if (!(protected_fraction > 0.0) || protected_fraction >= 1.0) {
        return NULL;
    }

    slot_count = capacity + 1u;

    self = (pb_cache_w_tinylfu_state *)pb_alloc(allocator, sizeof(pb_cache_w_tinylfu_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    self->index.capacity = 0;
    self->free_count = 0;
    self->sampled = 0;

    /* The window holds at least one entry, and never the whole cache. */
    self->window_size = (uint32_t)((double)capacity * window_fraction);
    if (self->window_size == 0u) {
        self->window_size = 1u;
    }
    self->main_size = capacity - self->window_size;
    self->protected_size = (uint32_t)((double)self->main_size * protected_fraction);
    if (self->protected_size == 0u) {
        self->protected_size = 1u;
    }

    /* Eight sketch positions per cached entry, rounded up to a power of two so
     * the modulo is a mask. Sized in 64 bits: the four rows of four-bit
     * counters take 2 * width bytes, which reaches 2^31 at the capacity bound
     * and would wrap a uint32 on the way to it. */
    sketch_width = pb_wtlfu_next_power_of_two((uint64_t)capacity * 8u);
    self->sketch_width = (uint32_t)sketch_width;
    self->sketch_mask = (uint32_t)(sketch_width - 1u);
    self->sketch_bytes = (uint32_t)((PB_WTLFU_ROWS * sketch_width) / 2u);
    self->doorkeeper_mask = (uint32_t)(sketch_width - 1u);
    self->doorkeeper_bytes = (uint32_t)(sketch_width / 8u);

    /* Halve the counters every ten accesses per cached entry. */
    self->sample_limit = capacity * 10u;

    self->keys = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    self->next = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->prev = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->segment_of = (uint8_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint8_t));
    self->free_slots = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->sketch = (uint8_t *)pb_alloc(allocator, (size_t)self->sketch_bytes);
    self->doorkeeper = (uint8_t *)pb_alloc(allocator, (size_t)self->doorkeeper_bytes);

    if (self->keys == NULL || self->next == NULL || self->prev == NULL ||
        self->segment_of == NULL || self->free_slots == NULL || self->sketch == NULL ||
        self->doorkeeper == NULL || !pb_map_init(&self->index, slot_count, allocator)) {
        pb_wtlfu_release(self);
        return NULL;
    }

    for (slot = 0; slot < slot_count; ++slot) {
        self->next[slot] = PB_WTLFU_NIL;
        self->prev[slot] = PB_WTLFU_NIL;
        self->segment_of[slot] = (uint8_t)PB_WTLFU_SEGMENTS;
        self->free_slots[slot] = slot_count - 1u - slot;
    }
    self->free_count = slot_count;

    for (segment = 0; segment < PB_WTLFU_SEGMENTS; ++segment) {
        self->heads[segment] = PB_WTLFU_NIL;
        self->tails[segment] = PB_WTLFU_NIL;
        self->lengths[segment] = 0;
    }

    memset(self->sketch, 0, (size_t)self->sketch_bytes);
    memset(self->doorkeeper, 0, (size_t)self->doorkeeper_bytes);

    return (pb_cache *)self;
}

static void wtlfu_destroy(pb_cache *cache)
{
    pb_cache_w_tinylfu_state *self = (pb_cache_w_tinylfu_state *)cache;

    if (self == NULL) {
        return;
    }
    pb_wtlfu_release(self);
}

/* --- the sketch ----------------------------------------------------------- */

static uint32_t pb_wtlfu_position(const pb_cache_w_tinylfu_state *self, uint32_t digest,
                                  uint32_t row)
{
    return pb_mix32(digest ^ PB_WTLFU_SALT[row]) & self->sketch_mask;
}

static uint32_t pb_wtlfu_counter_at(const pb_cache_w_tinylfu_state *self, uint32_t index)
{
    uint8_t byte = self->sketch[index >> 1];

    return ((index & 1u) == 0u) ? (uint32_t)(byte & 0x0Fu) : (uint32_t)(byte >> 4);
}

static void pb_wtlfu_increment(pb_cache_w_tinylfu_state *self, uint32_t index)
{
    uint32_t byte_index = index >> 1;
    uint8_t byte = self->sketch[byte_index];

    if ((index & 1u) == 0u) {
        uint8_t value = (uint8_t)(byte & 0x0Fu);
        if (value < PB_WTLFU_MAX_COUNT) {
            self->sketch[byte_index] = (uint8_t)((byte & 0xF0u) | (uint8_t)(value + 1u));
        }
    } else {
        uint8_t value = (uint8_t)(byte >> 4);
        if (value < PB_WTLFU_MAX_COUNT) {
            self->sketch[byte_index] =
                (uint8_t)((byte & 0x0Fu) | (uint8_t)((uint8_t)(value + 1u) << 4));
        }
    }
}

static uint32_t pb_wtlfu_doorkeeper_position(const pb_cache_w_tinylfu_state *self, uint32_t digest,
                                             uint32_t which)
{
    return pb_mix32(digest ^ PB_WTLFU_SALT[which]) & self->doorkeeper_mask;
}

static bool pb_wtlfu_doorkeeper_test(const pb_cache_w_tinylfu_state *self, uint32_t digest)
{
    uint32_t which;

    for (which = 0; which < 2u; ++which) {
        uint32_t bit = pb_wtlfu_doorkeeper_position(self, digest, which);
        if ((self->doorkeeper[bit >> 3] & (uint8_t)(1u << (bit & 7u))) == 0u) {
            return false;
        }
    }
    return true;
}

static void pb_wtlfu_doorkeeper_set(pb_cache_w_tinylfu_state *self, uint32_t digest)
{
    uint32_t which;

    for (which = 0; which < 2u; ++which) {
        uint32_t bit = pb_wtlfu_doorkeeper_position(self, digest, which);
        self->doorkeeper[bit >> 3] =
            (uint8_t)(self->doorkeeper[bit >> 3] | (uint8_t)(1u << (bit & 7u)));
    }
}

/*
 * Halve every counter and forget the doorkeeper.
 *
 * This is what stops W-TinyLFU becoming LFU: a key popular an hour ago decays
 * instead of holding its place forever. Shifting right and masking with 0x77
 * halves both counters in a byte without letting the high nibble's low bit leak
 * into the low one.
 */
static void pb_wtlfu_age(pb_cache_w_tinylfu_state *self)
{
    uint32_t index;

    for (index = 0; index < self->sketch_bytes; ++index) {
        self->sketch[index] = (uint8_t)((self->sketch[index] >> 1) & 0x77u);
    }
    memset(self->doorkeeper, 0, (size_t)self->doorkeeper_bytes);
    self->sampled = 0;
}

/*
 * Note an access.
 *
 * The doorkeeper absorbs a key's first appearance, so the very large number of
 * keys seen exactly once never consume sketch counters at all.
 */
static void pb_wtlfu_record(pb_cache_w_tinylfu_state *self, uint64_t key)
{
    uint32_t digest = pb_mix32((uint32_t)key);

    if (!pb_wtlfu_doorkeeper_test(self, digest)) {
        pb_wtlfu_doorkeeper_set(self, digest);
    } else {
        uint32_t row;
        for (row = 0; row < PB_WTLFU_ROWS; ++row) {
            pb_wtlfu_increment(self, row * self->sketch_width + pb_wtlfu_position(self, digest, row));
        }
    }

    self->sampled += 1u;
    if (self->sampled >= self->sample_limit) {
        pb_wtlfu_age(self);
    }
}

/* The count-min estimate, plus one if the doorkeeper has seen the key. */
static uint32_t pb_wtlfu_estimate(const pb_cache_w_tinylfu_state *self, uint64_t key)
{
    uint32_t digest = pb_mix32((uint32_t)key);
    uint32_t smallest = PB_WTLFU_MAX_COUNT;
    uint32_t row;

    for (row = 0; row < PB_WTLFU_ROWS; ++row) {
        uint32_t count =
            pb_wtlfu_counter_at(self, row * self->sketch_width + pb_wtlfu_position(self, digest, row));
        if (count < smallest) {
            smallest = count;
        }
    }

    return pb_wtlfu_doorkeeper_test(self, digest) ? smallest + 1u : smallest;
}

/* --- segments ------------------------------------------------------------- */

static void pb_wtlfu_link_front(pb_cache_w_tinylfu_state *self, uint32_t segment, uint32_t slot)
{
    uint32_t head = self->heads[segment];

    self->prev[slot] = PB_WTLFU_NIL;
    self->next[slot] = head;
    if (head != PB_WTLFU_NIL) {
        self->prev[head] = slot;
    } else {
        self->tails[segment] = slot;
    }
    self->heads[segment] = slot;
    self->segment_of[slot] = (uint8_t)segment;
    self->lengths[segment] += 1u;
}

static void pb_wtlfu_unlink(pb_cache_w_tinylfu_state *self, uint32_t slot)
{
    uint32_t segment = self->segment_of[slot];
    uint32_t following = self->next[slot];
    uint32_t preceding = self->prev[slot];

    assert(segment < PB_WTLFU_SEGMENTS);

    if (preceding != PB_WTLFU_NIL) {
        self->next[preceding] = following;
    } else {
        self->heads[segment] = following;
    }
    if (following != PB_WTLFU_NIL) {
        self->prev[following] = preceding;
    } else {
        self->tails[segment] = preceding;
    }

    self->next[slot] = PB_WTLFU_NIL;
    self->prev[slot] = PB_WTLFU_NIL;
    self->segment_of[slot] = (uint8_t)PB_WTLFU_SEGMENTS;
    self->lengths[segment] -= 1u;
}

static void pb_wtlfu_move_to_front(pb_cache_w_tinylfu_state *self, uint32_t segment, uint32_t slot)
{
    if (self->heads[segment] == slot) {
        return;
    }
    pb_wtlfu_unlink(self, slot);
    pb_wtlfu_link_front(self, segment, slot);
}

/* Move the window's overflow into the main cache while it has room. */
static void pb_wtlfu_drain_window(pb_cache_w_tinylfu_state *self)
{
    while (self->lengths[PB_WTLFU_WINDOW] > self->window_size &&
           self->lengths[PB_WTLFU_PROBATION] + self->lengths[PB_WTLFU_PROTECTED] <
               self->main_size) {
        uint32_t promoted = self->tails[PB_WTLFU_WINDOW];
        pb_wtlfu_unlink(self, promoted);
        pb_wtlfu_link_front(self, PB_WTLFU_PROBATION, promoted);
    }
}

/* The main cache's next victim: probation's oldest, or protected's. */
static uint32_t pb_wtlfu_main_victim(const pb_cache_w_tinylfu_state *self)
{
    if (self->tails[PB_WTLFU_PROBATION] != PB_WTLFU_NIL) {
        return self->tails[PB_WTLFU_PROBATION];
    }
    if (self->tails[PB_WTLFU_PROTECTED] != PB_WTLFU_NIL) {
        return self->tails[PB_WTLFU_PROTECTED];
    }
    return self->tails[PB_WTLFU_WINDOW];
}

static uint64_t pb_wtlfu_release_slot(pb_cache_w_tinylfu_state *self, uint32_t slot)
{
    uint64_t key = self->keys[slot];

    pb_wtlfu_unlink(self, slot);
    (void)pb_map_remove(&self->index, key);
    self->free_slots[self->free_count] = slot;
    self->free_count += 1u;
    return key;
}

static void wtlfu_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_w_tinylfu_state *self = (pb_cache_w_tinylfu_state *)cache;
    uint32_t slot;

    (void)meta;
    assert(self != NULL);

    /* Every access is evidence, whether or not the key is resident. Counting
     * misses is what lets a key earn admission before it is ever cached. */
    pb_wtlfu_record(self, key);

    if (hit) {
        uint32_t segment;

        if (!pb_map_get(&self->index, key, &slot)) {
            assert(false); /* the caller's residency tracking is wrong */
            return;
        }

        segment = self->segment_of[slot];
        if (segment == PB_WTLFU_WINDOW) {
            pb_wtlfu_move_to_front(self, PB_WTLFU_WINDOW, slot);
            return;
        }
        if (segment == PB_WTLFU_PROBATION) {
            /* A second hit promotes the entry out of reach of the contest. */
            pb_wtlfu_unlink(self, slot);
            pb_wtlfu_link_front(self, PB_WTLFU_PROTECTED, slot);
            if (self->lengths[PB_WTLFU_PROTECTED] > self->protected_size) {
                uint32_t demoted = self->tails[PB_WTLFU_PROTECTED];
                pb_wtlfu_unlink(self, demoted);
                pb_wtlfu_link_front(self, PB_WTLFU_PROBATION, demoted);
            }
            return;
        }
        pb_wtlfu_move_to_front(self, PB_WTLFU_PROTECTED, slot);
        return;
    }

    assert(self->free_count > 0u);
    if (self->free_count == 0u) {
        return;
    }

    /* New keys always enter the window; admission is contested later. */
    self->free_count -= 1u;
    slot = self->free_slots[self->free_count];
    self->keys[slot] = key;
    (void)pb_map_put(&self->index, key, slot);
    pb_wtlfu_link_front(self, PB_WTLFU_WINDOW, slot);

    /* The window keeps its size continuously, not only when the cache is full. */
    pb_wtlfu_drain_window(self);
}

static uint64_t wtlfu_evict(pb_cache *cache)
{
    pb_cache_w_tinylfu_state *self = (pb_cache_w_tinylfu_state *)cache;
    uint32_t victim;

    assert(self != NULL);

    /* Normally a no-op here: the window is drained on insertion. */
    pb_wtlfu_drain_window(self);

    if (self->lengths[PB_WTLFU_WINDOW] > self->window_size) {
        uint32_t candidate = self->tails[PB_WTLFU_WINDOW];

        victim = pb_wtlfu_main_victim(self);
        assert(victim != PB_WTLFU_NIL);
        if (victim == PB_WTLFU_NIL) {
            return 0u;
        }

        /*
         * Strictly greater: on a tie the incumbent stays. A resident entry has
         * demonstrated its frequency while the candidate has only an estimate,
         * and admitting on equal evidence would let a stream of one-hit wonders
         * churn the cache.
         */
        if (pb_wtlfu_estimate(self, self->keys[candidate]) >
            pb_wtlfu_estimate(self, self->keys[victim])) {
            pb_wtlfu_unlink(self, candidate);
            pb_wtlfu_link_front(self, PB_WTLFU_PROBATION, candidate);
            return pb_wtlfu_release_slot(self, victim);
        }
        return pb_wtlfu_release_slot(self, candidate);
    }

    victim = pb_wtlfu_main_victim(self);
    assert(victim != PB_WTLFU_NIL);
    if (victim == PB_WTLFU_NIL) {
        return 0u;
    }
    return pb_wtlfu_release_slot(self, victim);
}

static size_t wtlfu_memory_bytes(const pb_cache *cache)
{
    const pb_cache_w_tinylfu_state *self = (const pb_cache_w_tinylfu_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_w_tinylfu_state) +
           (size_t)self->slot_count *
               (sizeof(uint64_t) + 3u * sizeof(uint32_t) + sizeof(uint8_t)) +
           (size_t)self->sketch_bytes + (size_t)self->doorkeeper_bytes +
           pb_map_memory_bytes(&self->index);
}

const pb_cache_vtable pb_cache_w_tinylfu = {
    wtlfu_create,
    wtlfu_on_access,
    wtlfu_evict,
    NULL, /* admission is contested in evict, not at insertion */
    wtlfu_destroy,
    wtlfu_memory_bytes,
    false, /* allocates_after_create */
    "cache/w-tinylfu"
};

/* ========================================================================
 * src/ds/heap.c
 * ======================================================================== */

/* Children of i are 4i+1 .. 4i+4; the parent of i is (i-1)/4. */
#define PB_HEAP_ARITY 4u

/* True if (key_a, item_a) sorts before (key_b, item_b). */
static bool pb_heap_less(uint64_t key_a, uint32_t item_a, uint64_t key_b, uint32_t item_b)
{
    if (key_a != key_b) {
        return key_a < key_b;
    }
    /* Ties break by the lower item index — the registry's rule. */
    return item_a < item_b;
}

static void pb_heap_sift_up(pb_heap *heap, uint32_t index)
{
    uint64_t key = heap->keys[index];
    uint32_t item = heap->items[index];

    while (index > 0u) {
        uint32_t parent = (index - 1u) / PB_HEAP_ARITY;
        if (!pb_heap_less(key, item, heap->keys[parent], heap->items[parent])) {
            break;
        }
        heap->keys[index] = heap->keys[parent];
        heap->items[index] = heap->items[parent];
        index = parent;
    }

    heap->keys[index] = key;
    heap->items[index] = item;
}

static void pb_heap_sift_down(pb_heap *heap, uint32_t index)
{
    uint64_t key = heap->keys[index];
    uint32_t item = heap->items[index];

    for (;;) {
        uint32_t first = index * PB_HEAP_ARITY + 1u;
        uint32_t best;
        uint32_t child;
        uint32_t last;

        if (first >= heap->size) {
            break;
        }

        last = first + PB_HEAP_ARITY;
        if (last > heap->size) {
            last = heap->size;
        }

        best = first;
        for (child = first + 1u; child < last; ++child) {
            if (pb_heap_less(heap->keys[child], heap->items[child], heap->keys[best],
                             heap->items[best])) {
                best = child;
            }
        }

        if (!pb_heap_less(heap->keys[best], heap->items[best], key, item)) {
            break;
        }

        heap->keys[index] = heap->keys[best];
        heap->items[index] = heap->items[best];
        index = best;
    }

    heap->keys[index] = key;
    heap->items[index] = item;
}

bool pb_heap_init(pb_heap *heap, uint32_t capacity, const pb_allocator *allocator)
{
    assert(heap != NULL);

    heap->keys = NULL;
    heap->items = NULL;
    heap->size = 0;
    heap->capacity = 0;

    if (capacity == 0u) {
        return false;
    }

    heap->keys = (uint64_t *)pb_alloc(allocator, (size_t)capacity * sizeof(uint64_t));
    if (heap->keys == NULL) {
        return false;
    }
    heap->items = (uint32_t *)pb_alloc(allocator, (size_t)capacity * sizeof(uint32_t));
    if (heap->items == NULL) {
        pb_free(allocator, heap->keys, (size_t)capacity * sizeof(uint64_t));
        heap->keys = NULL;
        return false;
    }

    heap->capacity = capacity;
    return true;
}

void pb_heap_destroy(pb_heap *heap, const pb_allocator *allocator)
{
    if (heap == NULL || heap->capacity == 0u) {
        return;
    }

    pb_free(allocator, heap->keys, (size_t)heap->capacity * sizeof(uint64_t));
    pb_free(allocator, heap->items, (size_t)heap->capacity * sizeof(uint32_t));

    heap->keys = NULL;
    heap->items = NULL;
    heap->size = 0;
    heap->capacity = 0;
}

void pb_heap_clear(pb_heap *heap)
{
    assert(heap != NULL);
    heap->size = 0;
}

bool pb_heap_push(pb_heap *heap, uint64_t key, uint32_t item)
{
    assert(heap != NULL);

    if (heap->size >= heap->capacity) {
        return false;
    }

    heap->keys[heap->size] = key;
    heap->items[heap->size] = item;
    heap->size += 1u;
    pb_heap_sift_up(heap, heap->size - 1u);
    return true;
}

bool pb_heap_peek_min(const pb_heap *heap, uint64_t *key, uint32_t *item)
{
    assert(heap != NULL);

    if (heap->size == 0u) {
        return false;
    }
    if (key != NULL) {
        *key = heap->keys[0];
    }
    if (item != NULL) {
        *item = heap->items[0];
    }
    return true;
}

bool pb_heap_pop_min(pb_heap *heap, uint64_t *key, uint32_t *item)
{
    assert(heap != NULL);

    if (heap->size == 0u) {
        return false;
    }
    if (key != NULL) {
        *key = heap->keys[0];
    }
    if (item != NULL) {
        *item = heap->items[0];
    }

    heap->size -= 1u;
    if (heap->size > 0u) {
        heap->keys[0] = heap->keys[heap->size];
        heap->items[0] = heap->items[heap->size];
        pb_heap_sift_down(heap, 0u);
    }
    return true;
}

size_t pb_heap_memory_bytes(const pb_heap *heap)
{
    assert(heap != NULL);
    return (size_t)heap->capacity * (sizeof(uint64_t) + sizeof(uint32_t));
}

/* ========================================================================
 * src/ds/ilist.c
 * ======================================================================== */

bool pb_ilist_init(pb_ilist *list, uint32_t capacity, const pb_allocator *allocator)
{
    size_t bytes;

    assert(list != NULL);

    list->next = NULL;
    list->prev = NULL;
    list->head = PB_ILIST_NIL;
    list->tail = PB_ILIST_NIL;
    list->capacity = 0;
    list->length = 0;

    if (capacity == 0u || capacity > PB_ILIST_MAX_CAPACITY) {
        return false;
    }

    bytes = (size_t)capacity * sizeof(uint32_t);
    list->next = (uint32_t *)pb_alloc(allocator, bytes);
    if (list->next == NULL) {
        return false;
    }
    list->prev = (uint32_t *)pb_alloc(allocator, bytes);
    if (list->prev == NULL) {
        pb_free(allocator, list->next, bytes);
        list->next = NULL;
        return false;
    }

    list->capacity = capacity;
    pb_ilist_clear(list);
    return true;
}

void pb_ilist_destroy(pb_ilist *list, const pb_allocator *allocator)
{
    size_t bytes;

    if (list == NULL || list->capacity == 0u) {
        return;
    }

    bytes = (size_t)list->capacity * sizeof(uint32_t);
    pb_free(allocator, list->next, bytes);
    pb_free(allocator, list->prev, bytes);

    list->next = NULL;
    list->prev = NULL;
    list->head = PB_ILIST_NIL;
    list->tail = PB_ILIST_NIL;
    list->capacity = 0;
    list->length = 0;
}

void pb_ilist_clear(pb_ilist *list)
{
    uint32_t i;

    assert(list != NULL);

    for (i = 0; i < list->capacity; ++i) {
        list->next[i] = PB_ILIST_UNLINKED;
        list->prev[i] = PB_ILIST_UNLINKED;
    }
    list->head = PB_ILIST_NIL;
    list->tail = PB_ILIST_NIL;
    list->length = 0;
}

bool pb_ilist_contains(const pb_ilist *list, uint32_t node)
{
    assert(list != NULL);
    assert(node < list->capacity);
    return list->next[node] != PB_ILIST_UNLINKED;
}

void pb_ilist_push_front(pb_ilist *list, uint32_t node)
{
    assert(list != NULL);
    assert(node < list->capacity);
    assert(!pb_ilist_contains(list, node));

    list->prev[node] = PB_ILIST_NIL;
    list->next[node] = list->head;

    if (list->head != PB_ILIST_NIL) {
        list->prev[list->head] = node;
    } else {
        list->tail = node;
    }
    list->head = node;
    list->length += 1u;
}

void pb_ilist_push_back(pb_ilist *list, uint32_t node)
{
    assert(list != NULL);
    assert(node < list->capacity);
    assert(!pb_ilist_contains(list, node));

    list->next[node] = PB_ILIST_NIL;
    list->prev[node] = list->tail;

    if (list->tail != PB_ILIST_NIL) {
        list->next[list->tail] = node;
    } else {
        list->head = node;
    }
    list->tail = node;
    list->length += 1u;
}

void pb_ilist_remove(pb_ilist *list, uint32_t node)
{
    uint32_t next;
    uint32_t prev;

    assert(list != NULL);
    assert(node < list->capacity);
    assert(pb_ilist_contains(list, node));

    next = list->next[node];
    prev = list->prev[node];

    if (prev != PB_ILIST_NIL) {
        list->next[prev] = next;
    } else {
        list->head = next;
    }

    if (next != PB_ILIST_NIL) {
        list->prev[next] = prev;
    } else {
        list->tail = prev;
    }

    list->next[node] = PB_ILIST_UNLINKED;
    list->prev[node] = PB_ILIST_UNLINKED;
    list->length -= 1u;
}

void pb_ilist_move_to_front(pb_ilist *list, uint32_t node)
{
    assert(list != NULL);
    assert(node < list->capacity);
    assert(pb_ilist_contains(list, node));

    if (list->head == node) {
        return;
    }
    pb_ilist_remove(list, node);
    pb_ilist_push_front(list, node);
}

void pb_ilist_move_to_back(pb_ilist *list, uint32_t node)
{
    assert(list != NULL);
    assert(node < list->capacity);
    assert(pb_ilist_contains(list, node));

    if (list->tail == node) {
        return;
    }
    pb_ilist_remove(list, node);
    pb_ilist_push_back(list, node);
}

uint32_t pb_ilist_pop_front(pb_ilist *list)
{
    uint32_t node;

    assert(list != NULL);

    node = list->head;
    if (node == PB_ILIST_NIL) {
        return PB_ILIST_NIL;
    }
    pb_ilist_remove(list, node);
    return node;
}

uint32_t pb_ilist_pop_back(pb_ilist *list)
{
    uint32_t node;

    assert(list != NULL);

    node = list->tail;
    if (node == PB_ILIST_NIL) {
        return PB_ILIST_NIL;
    }
    pb_ilist_remove(list, node);
    return node;
}

size_t pb_ilist_memory_bytes(const pb_ilist *list)
{
    assert(list != NULL);
    return (size_t)list->capacity * sizeof(uint32_t) * 2u;
}

/* ========================================================================
 * src/ds/map.c
 * ======================================================================== */

/* Keep the table at most half full so linear probing stays short. */
#define PB_MAP_MIN_CAPACITY 8u

/*
 * splitmix64's finaliser, folded to 32 bits.
 *
 * Cache keys are frequently small integers or sequential ids, which land in
 * consecutive buckets under a weaker hash and turn linear probing into a linear
 * scan. This mixes them properly. The choice does not affect any policy's
 * decisions — only how quickly it finds them — because iteration order is never
 * exposed.
 */
static uint32_t pb_map_hash(uint64_t key)
{
    uint64_t z = key + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    return (uint32_t)z;
}

bool pb_map_init(pb_map *map, uint32_t min_entries, const pb_allocator *allocator)
{
    uint64_t sized = PB_MAP_MIN_CAPACITY;
    uint32_t capacity;

    assert(map != NULL);

    map->keys = NULL;
    map->values = NULL;
    map->occupied = NULL;
    map->mask = 0;
    map->capacity = 0;
    map->count = 0;

    if (min_entries == 0u) {
        return false;
    }

    /* Smallest power of two with room for min_entries at 50% load. Sized in
     * 64 bits: `min_entries * 2` wraps a uint32 from 2^31 entries up, and no
     * uint32-indexed table can serve those anyway. */
    while (sized < (uint64_t)min_entries * 2u) {
        sized *= 2u;
    }
    if (sized > (UINT64_C(1) << 31)) {
        return false; /* the request cannot be sized, let alone allocated */
    }
    capacity = (uint32_t)sized;

    map->keys = (uint64_t *)pb_alloc(allocator, (size_t)capacity * sizeof(uint64_t));
    if (map->keys == NULL) {
        return false;
    }
    map->values = (uint32_t *)pb_alloc(allocator, (size_t)capacity * sizeof(uint32_t));
    if (map->values == NULL) {
        pb_free(allocator, map->keys, (size_t)capacity * sizeof(uint64_t));
        map->keys = NULL;
        return false;
    }
    map->occupied = (uint8_t *)pb_alloc(allocator, (size_t)capacity * sizeof(uint8_t));
    if (map->occupied == NULL) {
        pb_free(allocator, map->keys, (size_t)capacity * sizeof(uint64_t));
        pb_free(allocator, map->values, (size_t)capacity * sizeof(uint32_t));
        map->keys = NULL;
        map->values = NULL;
        return false;
    }

    map->capacity = capacity;
    map->mask = capacity - 1u;
    pb_map_clear(map);
    return true;
}

void pb_map_destroy(pb_map *map, const pb_allocator *allocator)
{
    if (map == NULL || map->capacity == 0u) {
        return;
    }

    pb_free(allocator, map->keys, (size_t)map->capacity * sizeof(uint64_t));
    pb_free(allocator, map->values, (size_t)map->capacity * sizeof(uint32_t));
    pb_free(allocator, map->occupied, (size_t)map->capacity * sizeof(uint8_t));

    map->keys = NULL;
    map->values = NULL;
    map->occupied = NULL;
    map->mask = 0;
    map->capacity = 0;
    map->count = 0;
}

void pb_map_clear(pb_map *map)
{
    assert(map != NULL);
    if (map->capacity > 0u) {
        memset(map->occupied, 0, (size_t)map->capacity * sizeof(uint8_t));
    }
    map->count = 0;
}

bool pb_map_get(const pb_map *map, uint64_t key, uint32_t *value)
{
    uint32_t slot;

    assert(map != NULL);
    if (map->capacity == 0u) {
        return false;
    }

    slot = pb_map_hash(key) & map->mask;
    while (map->occupied[slot] != 0u) {
        if (map->keys[slot] == key) {
            if (value != NULL) {
                *value = map->values[slot];
            }
            return true;
        }
        slot = (slot + 1u) & map->mask;
    }
    return false;
}

bool pb_map_put(pb_map *map, uint64_t key, uint32_t value)
{
    uint32_t slot;
    uint32_t probes;

    assert(map != NULL);
    if (map->capacity == 0u) {
        return false;
    }

    /* The probe is bounded by the table: in a full table no slot is ever
     * empty, and an unbounded walk would cycle forever. */
    slot = pb_map_hash(key) & map->mask;
    for (probes = 0u; probes < map->capacity; ++probes) {
        if (map->occupied[slot] == 0u) {
            map->occupied[slot] = 1u;
            map->keys[slot] = key;
            map->values[slot] = value;
            map->count += 1u;
            return true;
        }
        if (map->keys[slot] == key) {
            map->values[slot] = value;
            return true;
        }
        slot = (slot + 1u) & map->mask;
    }

    return false; /* every slot holds some other key */
}

bool pb_map_remove(pb_map *map, uint64_t key)
{
    uint32_t hole;
    uint32_t probe;

    assert(map != NULL);
    if (map->capacity == 0u) {
        return false;
    }

    hole = pb_map_hash(key) & map->mask;
    while (map->occupied[hole] != 0u) {
        if (map->keys[hole] == key) {
            break;
        }
        hole = (hole + 1u) & map->mask;
    }
    if (map->occupied[hole] == 0u) {
        return false;
    }

    map->occupied[hole] = 0u;
    map->count -= 1u;

    /*
     * Backward-shift deletion. Walk forward from the hole; any entry whose
     * ideal slot is at or before the hole (cyclically) can be moved back into
     * it, which keeps every probe chain contiguous and leaves no tombstone.
     */
    probe = hole;
    for (;;) {
        uint32_t ideal;

        probe = (probe + 1u) & map->mask;
        if (map->occupied[probe] == 0u) {
            break;
        }

        ideal = pb_map_hash(map->keys[probe]) & map->mask;

        /* Is `hole` inside the span [ideal, probe] once wrap-around is taken
         * into account? If so, moving the entry back keeps it findable. */
        if ((probe > hole) ? (ideal <= hole || ideal > probe)
                           : (ideal <= hole && ideal > probe)) {
            map->keys[hole] = map->keys[probe];
            map->values[hole] = map->values[probe];
            map->occupied[hole] = 1u;
            map->occupied[probe] = 0u;
            hole = probe;
        }
    }

    return true;
}

size_t pb_map_memory_bytes(const pb_map *map)
{
    assert(map != NULL);
    return (size_t)map->capacity *
           (sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint8_t));
}

/* ========================================================================
 * src/ds/ring.c
 * ======================================================================== */

bool pb_ring_init(pb_ring *ring, uint32_t capacity, const pb_allocator *allocator)
{
    assert(ring != NULL);

    ring->slots = NULL;
    ring->head = 0;
    ring->length = 0;
    ring->capacity = 0;

    if (capacity == 0u) {
        return false;
    }

    ring->slots = (uint32_t *)pb_alloc(allocator, (size_t)capacity * sizeof(uint32_t));
    if (ring->slots == NULL) {
        return false;
    }

    ring->capacity = capacity;
    return true;
}

void pb_ring_destroy(pb_ring *ring, const pb_allocator *allocator)
{
    if (ring == NULL || ring->capacity == 0u) {
        return;
    }

    pb_free(allocator, ring->slots, (size_t)ring->capacity * sizeof(uint32_t));
    ring->slots = NULL;
    ring->head = 0;
    ring->length = 0;
    ring->capacity = 0;
}

void pb_ring_clear(pb_ring *ring)
{
    assert(ring != NULL);
    ring->head = 0;
    ring->length = 0;
}

bool pb_ring_push_back(pb_ring *ring, uint32_t value)
{
    uint32_t tail;

    assert(ring != NULL);

    if (ring->length >= ring->capacity) {
        return false;
    }

    /* head + length may wrap; the modulo keeps it inside the array. */
    tail = ring->head + ring->length;
    if (tail >= ring->capacity) {
        tail -= ring->capacity;
    }

    ring->slots[tail] = value;
    ring->length += 1u;
    return true;
}

uint32_t pb_ring_pop_front(pb_ring *ring)
{
    uint32_t value;

    assert(ring != NULL);

    if (ring->length == 0u) {
        return PB_RING_NIL;
    }

    value = ring->slots[ring->head];
    ring->head += 1u;
    if (ring->head >= ring->capacity) {
        ring->head = 0;
    }
    ring->length -= 1u;
    return value;
}

uint32_t pb_ring_peek_front(const pb_ring *ring)
{
    assert(ring != NULL);

    if (ring->length == 0u) {
        return PB_RING_NIL;
    }
    return ring->slots[ring->head];
}

uint32_t pb_ring_at(const pb_ring *ring, uint32_t offset)
{
    uint32_t index;

    assert(ring != NULL);

    if (offset >= ring->length) {
        return PB_RING_NIL;
    }

    index = ring->head + offset;
    if (index >= ring->capacity) {
        index -= ring->capacity;
    }
    return ring->slots[index];
}

size_t pb_ring_memory_bytes(const pb_ring *ring)
{
    assert(ring != NULL);
    return (size_t)ring->capacity * sizeof(uint32_t);
}

/* ========================================================================
 * src/kv_cache/h2o.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/h2o/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * H2O — keep the tokens that have received the most attention so far.
 *
 * Mirrors index.ts and policy.py. Three parallel arrays indexed by kept order,
 * which is the order the attention arrives in, so no lookup is ever needed on
 * the decode path.
 */




typedef struct pb_kvcache_h2o_state {
    const pb_allocator *allocator;
    uint32_t recent_window;
    uint32_t capacity; /* budget + 1 */
    uint32_t size;
    uint32_t *positions; /* kept positions, ascending */
    double *scores;      /* cumulative attention, parallel to positions */
    uint8_t *doomed;     /* eviction scratch, so evict allocates nothing */
} pb_kvcache_h2o_state;

static void h2o_release(pb_kvcache_h2o_state *self)
{
    size_t slots;

    if (self == NULL) {
        return;
    }
    slots = (size_t)self->capacity;
    pb_free(self->allocator, self->positions, slots * sizeof(uint32_t));
    pb_free(self->allocator, self->scores, slots * sizeof(double));
    pb_free(self->allocator, self->doomed, slots * sizeof(uint8_t));
    pb_free(self->allocator, self, sizeof(pb_kvcache_h2o_state));
}

static pb_kvcache *h2o_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_kvcache_h2o_params *config = (const pb_kvcache_h2o_params *)params;
    pb_kvcache_h2o_state *self;
    uint32_t budget;
    uint32_t recent_window;
    size_t slots;

    (void)rng; /* entirely deterministic */

    budget = (config == NULL) ? 512u : config->budget;
    recent_window = (config == NULL) ? 32u : config->recent_window;
    /* The slot arrays hold budget + 1 entries; UINT32_MAX would wrap that. */
    if (budget == 0u || budget == UINT32_MAX) {
        return NULL;
    }
    /* With the whole budget protected there would be nothing left to evict. */
    if (recent_window >= budget) {
        return NULL;
    }

    self = (pb_kvcache_h2o_state *)pb_alloc(allocator, sizeof(pb_kvcache_h2o_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->recent_window = recent_window;
    self->capacity = budget + 1u;
    self->size = 0u;
    slots = (size_t)self->capacity;

    self->positions = (uint32_t *)pb_alloc(allocator, slots * sizeof(uint32_t));
    self->scores = (double *)pb_alloc(allocator, slots * sizeof(double));
    self->doomed = (uint8_t *)pb_alloc(allocator, slots * sizeof(uint8_t));
    if (self->positions == NULL || self->scores == NULL || self->doomed == NULL) {
        h2o_release(self);
        return NULL;
    }

    /* Position 0's token exists before the first decode step (see kv_cache.h),
     * and starts with no attention to its name. */
    self->positions[0] = 0u;
    self->scores[0] = 0.0;
    self->doomed[0] = 0u;
    self->size = 1u;
    return (pb_kvcache *)self;
}

static void h2o_destroy(pb_kvcache *policy)
{
    h2o_release((pb_kvcache_h2o_state *)policy);
}

static void h2o_on_decode_step(pb_kvcache *policy, uint32_t pos, const float *attn,
                               size_t attn_len)
{
    pb_kvcache_h2o_state *self = (pb_kvcache_h2o_state *)policy;
    size_t shared;
    size_t i;

    assert(self != NULL);

    /* attn[i] belongs to the i-th kept position in ascending order, which is
     * exactly how `positions` is stored — the two are index-aligned. */
    if (attn != NULL) {
        shared = (attn_len < (size_t)self->size) ? attn_len : (size_t)self->size;
        for (i = 0; i < shared; ++i) {
            self->scores[i] = self->scores[i] + (double)attn[i];
        }
    }

    /* Holding more than budget + 1 means the caller's budget does not match the
     * policy's, which is a configuration error rather than a decision to make. */
    assert(self->size < self->capacity);
    if (self->size >= self->capacity) {
        return;
    }

    self->positions[self->size] = pos;
    self->scores[self->size] = 0.0;
    self->doomed[self->size] = 0u;
    self->size += 1u;
}

static size_t h2o_evict(pb_kvcache *policy, uint32_t budget, uint32_t *victims, size_t capacity)
{
    pb_kvcache_h2o_state *self = (pb_kvcache_h2o_state *)policy;
    uint32_t protected_count;
    uint32_t evictable_end;
    uint32_t needed;
    uint32_t taken;
    uint32_t read;
    uint32_t write = 0u;
    size_t written = 0;

    assert(self != NULL);
    assert(victims != NULL);

    if (self->size <= budget) {
        return 0;
    }
    needed = self->size - budget;

    /* The recent window is the tail of the ascending array, so everything
     * before evictable_end is fair game and nothing after it is. */
    protected_count = (self->recent_window < self->size) ? self->recent_window : self->size;
    evictable_end = self->size - protected_count;
    if (needed > evictable_end) {
        needed = evictable_end;
    }

    /* Refusing outright rather than evicting as much as fits: a partial
     * eviction would leave the caller over budget with no way to tell that the
     * buffer, not the policy, was the limit. */
    if ((size_t)needed > capacity) {
        return 0;
    }

    /* Repeated argmin rather than a sort: in steady state exactly one position
     * goes per step, so this is a single linear scan. */
    for (taken = 0u; taken < needed; ++taken) {
        uint32_t best = evictable_end; /* sentinel: nothing chosen yet */
        uint32_t i;
        for (i = 0u; i < evictable_end; ++i) {
            if (self->doomed[i] != 0u) {
                continue;
            }
            /* Strictly less, so a tie leaves the earlier index standing — the
             * lower position, since the array is ascending. */
            if (best == evictable_end || self->scores[i] < self->scores[best]) {
                best = i;
            }
        }
        if (best == evictable_end) {
            break;
        }
        self->doomed[best] = 1u;
    }

    /* One compacting pass: victims come out in ascending position order. */
    for (read = 0u; read < self->size; ++read) {
        if (self->doomed[read] != 0u) {
            self->doomed[read] = 0u;
            victims[written] = self->positions[read];
            written += 1;
            continue;
        }
        self->positions[write] = self->positions[read];
        self->scores[write] = self->scores[read];
        write += 1u;
    }
    self->size = write;
    return written;
}

static size_t h2o_memory_bytes(const pb_kvcache *policy)
{
    const pb_kvcache_h2o_state *self = (const pb_kvcache_h2o_state *)policy;
    size_t slots;

    if (self == NULL) {
        return sizeof(pb_kvcache_h2o_state);
    }
    slots = (size_t)self->capacity;
    return sizeof(pb_kvcache_h2o_state) + slots * sizeof(uint32_t) + slots * sizeof(double) +
           slots * sizeof(uint8_t);
}

const pb_kvcache_vtable pb_kvcache_h2o = { h2o_create, h2o_on_decode_step, h2o_evict,
                                           h2o_memory_bytes, h2o_destroy };

/* ========================================================================
 * src/kv_cache/pyramidkv.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/pyramidkv/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * PyramidKV — spend more cache on early layers than late ones.
 *
 * Mirrors index.ts and policy.py. The selection machinery is snapkv's; what is
 * new is `pb_kvcache_pyramid_budget` and the eviction target, which is the
 * tighter of the caller's budget and this layer's share.
 */




typedef struct pb_kvcache_pyramidkv_state {
    const pb_allocator *allocator;
    uint32_t effective; /* this layer's share, after redistribution */
    uint32_t recent_window;
    uint32_t obs_window;
    uint32_t pool_radius;
    uint32_t capacity;
    uint32_t size;
    uint32_t slot;
    uint32_t *positions;
    float *history; /* capacity * obs_window, position-major */
    double *sums;
    double *pooled;
    uint8_t *doomed;
} pb_kvcache_pyramidkv_state;

uint32_t pb_kvcache_pyramid_budget(uint32_t budget, uint32_t layer, uint32_t num_layers,
                                   uint32_t pyramid_ratio)
{
    uint64_t span;
    uint64_t numerator;
    uint64_t denominator;

    /* One layer has nothing to redistribute, and the denominator would be
     * zero. */
    if (num_layers <= 1u) {
        return budget;
    }

    /* uint64 throughout: the numerator reaches 2*budget*ratio*num_layers, which
     * overflows 32 bits for a large model at a large budget. */
    span = (uint64_t)num_layers - 1u;
    numerator = 2ull * (uint64_t)budget *
                ((uint64_t)pyramid_ratio * span - (uint64_t)layer * ((uint64_t)pyramid_ratio - 1u));
    denominator = ((uint64_t)pyramid_ratio + 1u) * span;
    return (uint32_t)(numerator / denominator);
}

static void pyramidkv_release(pb_kvcache_pyramidkv_state *self)
{
    size_t slots;

    if (self == NULL) {
        return;
    }
    slots = (size_t)self->capacity;
    pb_free(self->allocator, self->positions, slots * sizeof(uint32_t));
    pb_free(self->allocator, self->history,
            slots * (size_t)self->obs_window * sizeof(float));
    pb_free(self->allocator, self->sums, slots * sizeof(double));
    pb_free(self->allocator, self->pooled, slots * sizeof(double));
    pb_free(self->allocator, self->doomed, slots * sizeof(uint8_t));
    pb_free(self->allocator, self, sizeof(pb_kvcache_pyramidkv_state));
}

static pb_kvcache *pyramidkv_create(const void *params, const pb_allocator *allocator,
                                    pb_rng *rng)
{
    const pb_kvcache_pyramidkv_params *config = (const pb_kvcache_pyramidkv_params *)params;
    pb_kvcache_pyramidkv_state *self;
    uint32_t budget;
    uint32_t layer;
    uint32_t num_layers;
    uint32_t pyramid_ratio;
    uint32_t recent_window;
    uint32_t obs_window;
    uint32_t pool_kernel;
    uint32_t allocated;
    uint32_t effective;
    size_t slots;

    (void)rng; /* entirely deterministic */

    budget = (config == NULL) ? 512u : config->budget;
    layer = (config == NULL) ? 0u : config->layer;
    num_layers = (config == NULL) ? 1u : config->num_layers;
    pyramid_ratio = (config == NULL) ? 4u : config->pyramid_ratio;
    recent_window = (config == NULL) ? 32u : config->recent_window;
    obs_window = (config == NULL) ? 16u : config->obs_window;
    pool_kernel = (config == NULL) ? 7u : config->pool_kernel;

    /* The slot arrays hold at least budget + 1 entries; UINT32_MAX would wrap
     * that. */
    if (budget == 0u || budget == UINT32_MAX || num_layers == 0u || obs_window == 0u) {
        return NULL;
    }
    if (layer >= num_layers) {
        return NULL;
    }
    /* A ratio below one would invert the pyramid, which is a different policy. */
    if (pyramid_ratio == 0u) {
        return NULL;
    }
    if (recent_window >= budget) {
        return NULL;
    }
    if (pool_kernel == 0u || (pool_kernel % 2u) == 0u) {
        return NULL;
    }

    /* A deep layer's share can fall below the recent window, at which point the
     * cache would be smaller than its own protected region — a state the
     * selection rule cannot express. It keeps the window plus one instead. */
    allocated = pb_kvcache_pyramid_budget(budget, layer, num_layers, pyramid_ratio);
    effective = (allocated < recent_window + 1u) ? recent_window + 1u : allocated;

    self = (pb_kvcache_pyramidkv_state *)pb_alloc(allocator,
                                                  sizeof(pb_kvcache_pyramidkv_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->effective = effective;
    self->recent_window = recent_window;
    self->obs_window = obs_window;
    self->pool_radius = (pool_kernel - 1u) / 2u;
    /* Room for whichever cap is larger: a shallow layer's share exceeds the
     * average, and a caller may still drive this at the average budget. */
    self->capacity = ((effective > budget) ? effective : budget) + 1u;
    self->size = 0u;
    self->slot = 0u;
    slots = (size_t)self->capacity;

    self->positions = (uint32_t *)pb_alloc(allocator, slots * sizeof(uint32_t));
    self->history =
        (float *)pb_alloc(allocator, slots * (size_t)obs_window * sizeof(float));
    self->sums = (double *)pb_alloc(allocator, slots * sizeof(double));
    self->pooled = (double *)pb_alloc(allocator, slots * sizeof(double));
    self->doomed = (uint8_t *)pb_alloc(allocator, slots * sizeof(uint8_t));
    if (self->positions == NULL || self->history == NULL || self->sums == NULL ||
        self->pooled == NULL || self->doomed == NULL) {
        pyramidkv_release(self);
        return NULL;
    }

    memset(self->history, 0, (size_t)obs_window * sizeof(float));
    self->positions[0] = 0u;
    self->doomed[0] = 0u;
    self->size = 1u;
    return (pb_kvcache *)self;
}

static void pyramidkv_destroy(pb_kvcache *policy)
{
    pyramidkv_release((pb_kvcache_pyramidkv_state *)policy);
}

static void pyramidkv_on_decode_step(pb_kvcache *policy, uint32_t pos, const float *attn,
                                     size_t attn_len)
{
    pb_kvcache_pyramidkv_state *self = (pb_kvcache_pyramidkv_state *)policy;
    size_t shared;
    size_t i;

    assert(self != NULL);

    /* A null vector is inert, ring included: the window spans the last
     * obs_window observed steps, not the last obs_window calls. */
    if (attn != NULL) {
        shared = (attn_len < (size_t)self->size) ? attn_len : (size_t)self->size;
        for (i = 0; i < shared; ++i) {
            self->history[i * (size_t)self->obs_window + (size_t)self->slot] = attn[i];
        }

        self->slot += 1u;
        if (self->slot == self->obs_window) {
            self->slot = 0u;
        }
    }

    assert(self->size < self->capacity);
    if (self->size >= self->capacity) {
        return;
    }

    memset(&self->history[(size_t)self->size * (size_t)self->obs_window], 0,
           (size_t)self->obs_window * sizeof(float));
    self->positions[self->size] = pos;
    self->doomed[self->size] = 0u;
    self->size += 1u;
}

static size_t pyramidkv_evict(pb_kvcache *policy, uint32_t budget, uint32_t *victims,
                              size_t capacity)
{
    pb_kvcache_pyramidkv_state *self = (pb_kvcache_pyramidkv_state *)policy;
    uint32_t target;
    uint32_t protected_count;
    uint32_t evictable_end;
    uint32_t needed;
    uint32_t taken;
    uint32_t i;
    uint32_t read;
    uint32_t write = 0u;
    size_t written = 0;

    assert(self != NULL);
    assert(victims != NULL);

    /* The tighter of the caller's budget and this layer's share, so a deep
     * layer holds less than it was offered while never exceeding the ask. */
    target = (budget < self->effective) ? budget : self->effective;
    if (self->size <= target) {
        return 0;
    }
    needed = self->size - target;

    protected_count = (self->recent_window < self->size) ? self->recent_window : self->size;
    evictable_end = self->size - protected_count;
    if (needed > evictable_end) {
        needed = evictable_end;
    }

    if ((size_t)needed > capacity) {
        return 0;
    }

    for (i = 0u; i < self->size; ++i) {
        const float *record = &self->history[(size_t)i * (size_t)self->obs_window];
        double total = 0.0;
        uint32_t s;
        for (s = 0u; s < self->obs_window; ++s) {
            total += (double)record[s];
        }
        self->sums[i] = total;
    }

    for (i = 0u; i < self->size; ++i) {
        uint32_t lo = (i < self->pool_radius) ? 0u : i - self->pool_radius;
        uint32_t hi = i + self->pool_radius;
        double best;
        uint32_t j;
        if (hi >= self->size) {
            hi = self->size - 1u;
        }
        best = self->sums[lo];
        for (j = lo + 1u; j <= hi; ++j) {
            if (self->sums[j] > best) {
                best = self->sums[j];
            }
        }
        self->pooled[i] = best;
    }

    for (taken = 0u; taken < needed; ++taken) {
        uint32_t best = evictable_end;
        for (i = 0u; i < evictable_end; ++i) {
            if (self->doomed[i] != 0u) {
                continue;
            }
            if (best == evictable_end || self->pooled[i] < self->pooled[best]) {
                best = i;
            }
        }
        if (best == evictable_end) {
            break;
        }
        self->doomed[best] = 1u;
    }

    for (read = 0u; read < self->size; ++read) {
        if (self->doomed[read] != 0u) {
            self->doomed[read] = 0u;
            victims[written] = self->positions[read];
            written += 1;
            continue;
        }
        if (write != read) {
            self->positions[write] = self->positions[read];
            memmove(&self->history[(size_t)write * (size_t)self->obs_window],
                    &self->history[(size_t)read * (size_t)self->obs_window],
                    (size_t)self->obs_window * sizeof(float));
        }
        write += 1u;
    }
    self->size = write;
    return written;
}

static size_t pyramidkv_memory_bytes(const pb_kvcache *policy)
{
    const pb_kvcache_pyramidkv_state *self = (const pb_kvcache_pyramidkv_state *)policy;
    size_t slots;

    if (self == NULL) {
        return sizeof(pb_kvcache_pyramidkv_state);
    }
    slots = (size_t)self->capacity;
    return sizeof(pb_kvcache_pyramidkv_state) + slots * sizeof(uint32_t) +
           slots * (size_t)self->obs_window * sizeof(float) + slots * sizeof(double) +
           slots * sizeof(double) + slots * sizeof(uint8_t);
}

const pb_kvcache_vtable pb_kvcache_pyramidkv = { pyramidkv_create, pyramidkv_on_decode_step,
                                                 pyramidkv_evict, pyramidkv_memory_bytes,
                                                 pyramidkv_destroy };

/* ========================================================================
 * src/kv_cache/scissorhands.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/scissorhands/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * Scissorhands — count how many steps a token mattered for, not how much.
 *
 * Mirrors index.ts and policy.py. Structurally h2o with a vote counter in place
 * of a cumulative score, which is the whole of the difference between the two
 * policies and the reason they diverge on a stale early spike.
 */




typedef struct pb_kvcache_scissorhands_state {
    const pb_allocator *allocator;
    uint32_t recent_window;
    uint32_t capacity; /* budget + 1 */
    uint32_t size;
    uint32_t *positions; /* kept positions, ascending */
    uint32_t *votes;     /* steps beating the fair share, parallel to positions */
    uint8_t *doomed;     /* eviction scratch, so evict allocates nothing */
} pb_kvcache_scissorhands_state;

static void scissorhands_release(pb_kvcache_scissorhands_state *self)
{
    size_t slots;

    if (self == NULL) {
        return;
    }
    slots = (size_t)self->capacity;
    pb_free(self->allocator, self->positions, slots * sizeof(uint32_t));
    pb_free(self->allocator, self->votes, slots * sizeof(uint32_t));
    pb_free(self->allocator, self->doomed, slots * sizeof(uint8_t));
    pb_free(self->allocator, self, sizeof(pb_kvcache_scissorhands_state));
}

static pb_kvcache *scissorhands_create(const void *params, const pb_allocator *allocator,
                                       pb_rng *rng)
{
    const pb_kvcache_scissorhands_params *config =
        (const pb_kvcache_scissorhands_params *)params;
    pb_kvcache_scissorhands_state *self;
    uint32_t budget;
    uint32_t recent_window;
    size_t slots;

    (void)rng; /* entirely deterministic */

    budget = (config == NULL) ? 512u : config->budget;
    recent_window = (config == NULL) ? 32u : config->recent_window;
    /* The slot arrays hold budget + 1 entries; UINT32_MAX would wrap that. */
    if (budget == 0u || budget == UINT32_MAX) {
        return NULL;
    }
    /* With the whole budget protected there would be nothing left to evict. */
    if (recent_window >= budget) {
        return NULL;
    }

    self = (pb_kvcache_scissorhands_state *)pb_alloc(
        allocator, sizeof(pb_kvcache_scissorhands_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->recent_window = recent_window;
    self->capacity = budget + 1u;
    self->size = 0u;
    slots = (size_t)self->capacity;

    self->positions = (uint32_t *)pb_alloc(allocator, slots * sizeof(uint32_t));
    self->votes = (uint32_t *)pb_alloc(allocator, slots * sizeof(uint32_t));
    self->doomed = (uint8_t *)pb_alloc(allocator, slots * sizeof(uint8_t));
    if (self->positions == NULL || self->votes == NULL || self->doomed == NULL) {
        scissorhands_release(self);
        return NULL;
    }

    /* Position 0's token exists before the first decode step (see kv_cache.h),
     * and has voted on nothing yet. */
    self->positions[0] = 0u;
    self->votes[0] = 0u;
    self->doomed[0] = 0u;
    self->size = 1u;
    return (pb_kvcache *)self;
}

static void scissorhands_destroy(pb_kvcache *policy)
{
    scissorhands_release((pb_kvcache_scissorhands_state *)policy);
}

static void scissorhands_on_decode_step(pb_kvcache *policy, uint32_t pos, const float *attn,
                                        size_t attn_len)
{
    pb_kvcache_scissorhands_state *self = (pb_kvcache_scissorhands_state *)policy;
    double share;
    size_t shared;
    size_t i;

    assert(self != NULL);

    if (attn != NULL && attn_len > 0u) {
        /* One double division per step, not per position. */
        share = 1.0 / (double)attn_len;
        shared = (attn_len < (size_t)self->size) ? attn_len : (size_t)self->size;
        for (i = 0; i < shared; ++i) {
            /* Strictly greater: a position that exactly matches its share does
             * not vote, which a vector pins. */
            if ((double)attn[i] > share) {
                self->votes[i] += 1u;
            }
        }
    }

    /* Holding more than budget + 1 means the caller's budget does not match the
     * policy's, which is a configuration error rather than a decision to make. */
    assert(self->size < self->capacity);
    if (self->size >= self->capacity) {
        return;
    }

    self->positions[self->size] = pos;
    self->votes[self->size] = 0u;
    self->doomed[self->size] = 0u;
    self->size += 1u;
}

static size_t scissorhands_evict(pb_kvcache *policy, uint32_t budget, uint32_t *victims,
                                 size_t capacity)
{
    pb_kvcache_scissorhands_state *self = (pb_kvcache_scissorhands_state *)policy;
    uint32_t protected_count;
    uint32_t evictable_end;
    uint32_t needed;
    uint32_t taken;
    uint32_t read;
    uint32_t write = 0u;
    size_t written = 0;

    assert(self != NULL);
    assert(victims != NULL);

    if (self->size <= budget) {
        return 0;
    }
    needed = self->size - budget;

    protected_count = (self->recent_window < self->size) ? self->recent_window : self->size;
    evictable_end = self->size - protected_count;
    if (needed > evictable_end) {
        needed = evictable_end;
    }

    /* Refusing outright rather than evicting as much as fits: a partial
     * eviction would leave the caller over budget with no way to tell that the
     * buffer, not the policy, was the limit. */
    if ((size_t)needed > capacity) {
        return 0;
    }

    for (taken = 0u; taken < needed; ++taken) {
        uint32_t best = evictable_end; /* sentinel: nothing chosen yet */
        uint32_t i;
        for (i = 0u; i < evictable_end; ++i) {
            if (self->doomed[i] != 0u) {
                continue;
            }
            /* Strictly less, so a tie leaves the earlier index standing — the
             * lower position, since the array is ascending. */
            if (best == evictable_end || self->votes[i] < self->votes[best]) {
                best = i;
            }
        }
        if (best == evictable_end) {
            break;
        }
        self->doomed[best] = 1u;
    }

    /* One compacting pass: victims come out in ascending position order. */
    for (read = 0u; read < self->size; ++read) {
        if (self->doomed[read] != 0u) {
            self->doomed[read] = 0u;
            victims[written] = self->positions[read];
            written += 1;
            continue;
        }
        self->positions[write] = self->positions[read];
        self->votes[write] = self->votes[read];
        write += 1u;
    }
    self->size = write;
    return written;
}

static size_t scissorhands_memory_bytes(const pb_kvcache *policy)
{
    const pb_kvcache_scissorhands_state *self = (const pb_kvcache_scissorhands_state *)policy;
    size_t slots;

    if (self == NULL) {
        return sizeof(pb_kvcache_scissorhands_state);
    }
    slots = (size_t)self->capacity;
    return sizeof(pb_kvcache_scissorhands_state) + slots * sizeof(uint32_t) +
           slots * sizeof(uint32_t) + slots * sizeof(uint8_t);
}

const pb_kvcache_vtable pb_kvcache_scissorhands = { scissorhands_create,
                                                    scissorhands_on_decode_step,
                                                    scissorhands_evict,
                                                    scissorhands_memory_bytes,
                                                    scissorhands_destroy };

/* ========================================================================
 * src/kv_cache/sliding_window.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/sliding-window/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * Sliding window — keep the most recent tokens and forget the rest.
 *
 * Mirrors index.ts and policy.py. A ring buffer of positions: O(1) per step,
 * nothing allocated after create, and exactly budget + 1 slots because that is
 * the most that can be held before an eviction is asked for.
 */




typedef struct pb_kvcache_sliding_window_state {
    const pb_allocator *allocator;
    uint32_t capacity; /* budget + 1 */
    uint32_t *slots;   /* kept positions in arrival order, oldest at head */
    uint32_t head;
    uint32_t size;
} pb_kvcache_sliding_window_state;

static pb_kvcache *sliding_window_create(const void *params, const pb_allocator *allocator,
                                         pb_rng *rng)
{
    const pb_kvcache_sliding_window_params *config =
        (const pb_kvcache_sliding_window_params *)params;
    pb_kvcache_sliding_window_state *self;
    uint32_t budget;
    uint32_t capacity;

    (void)rng; /* entirely deterministic */

    budget = (config == NULL) ? 512u : config->budget;
    /* The ring holds budget + 1 slots; a budget of UINT32_MAX would wrap it. */
    if (budget == 0u || budget == UINT32_MAX) {
        return NULL;
    }
    capacity = budget + 1u;

    self = (pb_kvcache_sliding_window_state *)pb_alloc(
        allocator, sizeof(pb_kvcache_sliding_window_state));
    if (self == NULL) {
        return NULL;
    }

    self->slots = (uint32_t *)pb_alloc(allocator, (size_t)capacity * sizeof(uint32_t));
    if (self->slots == NULL) {
        pb_free(allocator, self, sizeof(pb_kvcache_sliding_window_state));
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->head = 0u;
    /* Position 0's token exists before the first decode step, so the cache
     * holds it from the outset (see kv_cache.h). */
    self->slots[0] = 0u;
    self->size = 1u;
    return (pb_kvcache *)self;
}

static void sliding_window_destroy(pb_kvcache *policy)
{
    pb_kvcache_sliding_window_state *self = (pb_kvcache_sliding_window_state *)policy;

    if (self == NULL) {
        return;
    }
    pb_free(self->allocator, self->slots, (size_t)self->capacity * sizeof(uint32_t));
    pb_free(self->allocator, self, sizeof(pb_kvcache_sliding_window_state));
}

static void sliding_window_on_decode_step(pb_kvcache *policy, uint32_t pos, const float *attn,
                                          size_t attn_len)
{
    pb_kvcache_sliding_window_state *self = (pb_kvcache_sliding_window_state *)policy;
    uint32_t slot;

    assert(self != NULL);

    /* Not reading the attention is the point: this policy cannot be accused of
     * using information it does not have. */
    (void)attn;
    (void)attn_len;

    /* Holding more than budget + 1 means the caller's budget does not match the
     * policy's, which is a configuration error rather than a decision to make. */
    assert(self->size < self->capacity);
    if (self->size >= self->capacity) {
        return;
    }

    slot = self->head + self->size;
    if (slot >= self->capacity) {
        slot -= self->capacity;
    }
    self->slots[slot] = pos;
    self->size += 1u;
}

static size_t sliding_window_evict(pb_kvcache *policy, uint32_t budget, uint32_t *victims,
                                   size_t capacity)
{
    pb_kvcache_sliding_window_state *self = (pb_kvcache_sliding_window_state *)policy;
    size_t written = 0;

    assert(self != NULL);
    assert(victims != NULL);

    /* Refusing outright rather than evicting as much as fits: a partial
     * eviction would leave the caller over budget with no way to tell that the
     * buffer, not the policy, was the limit. */
    if (self->size <= budget) {
        return 0;
    }
    if ((size_t)(self->size - budget) > capacity) {
        return 0;
    }

    while (self->size > budget) {
        victims[written] = self->slots[self->head];
        written += 1;
        self->head += 1u;
        if (self->head == self->capacity) {
            self->head = 0u;
        }
        self->size -= 1u;
    }
    return written;
}

static size_t sliding_window_memory_bytes(const pb_kvcache *policy)
{
    const pb_kvcache_sliding_window_state *self =
        (const pb_kvcache_sliding_window_state *)policy;

    if (self == NULL) {
        return sizeof(pb_kvcache_sliding_window_state);
    }
    return sizeof(pb_kvcache_sliding_window_state) +
           (size_t)self->capacity * sizeof(uint32_t);
}

const pb_kvcache_vtable pb_kvcache_sliding_window = { sliding_window_create,
                                                      sliding_window_on_decode_step,
                                                      sliding_window_evict,
                                                      sliding_window_memory_bytes,
                                                      sliding_window_destroy };

/* ========================================================================
 * src/kv_cache/snapkv.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/snapkv/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * SnapKV — score on the last few steps, then max-pool across neighbours.
 *
 * Mirrors index.ts and policy.py. The history is position-major
 * (history[i * obs_window + slot]) so compaction moves each surviving
 * position's whole record in one contiguous memmove.
 */




typedef struct pb_kvcache_snapkv_state {
    const pb_allocator *allocator;
    uint32_t recent_window;
    uint32_t obs_window;
    uint32_t pool_radius; /* (pool_kernel - 1) / 2 */
    uint32_t capacity;    /* budget + 1 */
    uint32_t size;
    uint32_t slot; /* ring slot the next step writes to */
    uint32_t *positions;
    float *history; /* capacity * obs_window, position-major */
    double *sums;   /* eviction scratch */
    double *pooled; /* eviction scratch */
    uint8_t *doomed;
} pb_kvcache_snapkv_state;

static void snapkv_release(pb_kvcache_snapkv_state *self)
{
    size_t slots;

    if (self == NULL) {
        return;
    }
    slots = (size_t)self->capacity;
    pb_free(self->allocator, self->positions, slots * sizeof(uint32_t));
    pb_free(self->allocator, self->history,
            slots * (size_t)self->obs_window * sizeof(float));
    pb_free(self->allocator, self->sums, slots * sizeof(double));
    pb_free(self->allocator, self->pooled, slots * sizeof(double));
    pb_free(self->allocator, self->doomed, slots * sizeof(uint8_t));
    pb_free(self->allocator, self, sizeof(pb_kvcache_snapkv_state));
}

static pb_kvcache *snapkv_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_kvcache_snapkv_params *config = (const pb_kvcache_snapkv_params *)params;
    pb_kvcache_snapkv_state *self;
    uint32_t budget;
    uint32_t recent_window;
    uint32_t obs_window;
    uint32_t pool_kernel;
    size_t slots;

    (void)rng; /* entirely deterministic */

    budget = (config == NULL) ? 512u : config->budget;
    recent_window = (config == NULL) ? 32u : config->recent_window;
    obs_window = (config == NULL) ? 16u : config->obs_window;
    pool_kernel = (config == NULL) ? 7u : config->pool_kernel;

    /* The slot arrays hold budget + 1 entries; UINT32_MAX would wrap that. */
    if (budget == 0u || budget == UINT32_MAX || obs_window == 0u) {
        return NULL;
    }
    if (recent_window >= budget) {
        return NULL;
    }
    /* An even kernel has no centre, so the neighbourhood would be lopsided. */
    if (pool_kernel == 0u || (pool_kernel % 2u) == 0u) {
        return NULL;
    }

    self = (pb_kvcache_snapkv_state *)pb_alloc(allocator, sizeof(pb_kvcache_snapkv_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->recent_window = recent_window;
    self->obs_window = obs_window;
    self->pool_radius = (pool_kernel - 1u) / 2u;
    self->capacity = budget + 1u;
    self->size = 0u;
    self->slot = 0u;
    slots = (size_t)self->capacity;

    self->positions = (uint32_t *)pb_alloc(allocator, slots * sizeof(uint32_t));
    self->history =
        (float *)pb_alloc(allocator, slots * (size_t)obs_window * sizeof(float));
    self->sums = (double *)pb_alloc(allocator, slots * sizeof(double));
    self->pooled = (double *)pb_alloc(allocator, slots * sizeof(double));
    self->doomed = (uint8_t *)pb_alloc(allocator, slots * sizeof(uint8_t));
    if (self->positions == NULL || self->history == NULL || self->sums == NULL ||
        self->pooled == NULL || self->doomed == NULL) {
        snapkv_release(self);
        return NULL;
    }

    /* Position 0's token exists before the first decode step (see kv_cache.h),
     * with an empty history. */
    memset(self->history, 0, (size_t)obs_window * sizeof(float));
    self->positions[0] = 0u;
    self->doomed[0] = 0u;
    self->size = 1u;
    return (pb_kvcache *)self;
}

static void snapkv_destroy(pb_kvcache *policy)
{
    snapkv_release((pb_kvcache_snapkv_state *)policy);
}

static void snapkv_on_decode_step(pb_kvcache *policy, uint32_t pos, const float *attn,
                                  size_t attn_len)
{
    pb_kvcache_snapkv_state *self = (pb_kvcache_snapkv_state *)policy;
    size_t shared;
    size_t i;

    assert(self != NULL);

    /* A null attention vector is entirely inert — nothing is written and the
     * ring does not advance — so the window spans the last obs_window
     * *observed* steps rather than the last obs_window calls. Advancing without
     * writing would leave a stale weight in the slot for another full cycle, so
     * a window claiming to cover the recent past would quietly sum values of
     * indeterminate age. */
    if (attn != NULL) {
        shared = (attn_len < (size_t)self->size) ? attn_len : (size_t)self->size;
        for (i = 0; i < shared; ++i) {
            self->history[i * (size_t)self->obs_window + (size_t)self->slot] = attn[i];
        }

        self->slot += 1u;
        if (self->slot == self->obs_window) {
            self->slot = 0u;
        }
    }

    assert(self->size < self->capacity);
    if (self->size >= self->capacity) {
        return;
    }

    memset(&self->history[(size_t)self->size * (size_t)self->obs_window], 0,
           (size_t)self->obs_window * sizeof(float));
    self->positions[self->size] = pos;
    self->doomed[self->size] = 0u;
    self->size += 1u;
}

static size_t snapkv_evict(pb_kvcache *policy, uint32_t budget, uint32_t *victims,
                           size_t capacity)
{
    pb_kvcache_snapkv_state *self = (pb_kvcache_snapkv_state *)policy;
    uint32_t protected_count;
    uint32_t evictable_end;
    uint32_t needed;
    uint32_t taken;
    uint32_t i;
    uint32_t read;
    uint32_t write = 0u;
    size_t written = 0;

    assert(self != NULL);
    assert(victims != NULL);

    if (self->size <= budget) {
        return 0;
    }
    needed = self->size - budget;

    protected_count = (self->recent_window < self->size) ? self->recent_window : self->size;
    evictable_end = self->size - protected_count;
    if (needed > evictable_end) {
        needed = evictable_end;
    }

    /* Refusing outright rather than evicting as much as fits: a partial
     * eviction would leave the caller over budget with no way to tell that the
     * buffer, not the policy, was the limit. */
    if ((size_t)needed > capacity) {
        return 0;
    }

    /* Sum each position's window from scratch rather than maintaining a running
     * total, which would drift — and the drift would have to be bit-identical
     * in three languages to stay reproducible. Slot order is fixed, index 0
     * upward rather than chronological: arbitrary but pinned. */
    for (i = 0u; i < self->size; ++i) {
        const float *record = &self->history[(size_t)i * (size_t)self->obs_window];
        double total = 0.0;
        uint32_t s;
        for (s = 0u; s < self->obs_window; ++s) {
            total += (double)record[s];
        }
        self->sums[i] = total;
    }

    /* Max-pool across neighbours, over the whole kept set: a protected recent
     * position is still a legitimate neighbour to inherit a score from. */
    for (i = 0u; i < self->size; ++i) {
        uint32_t lo = (i < self->pool_radius) ? 0u : i - self->pool_radius;
        uint32_t hi = i + self->pool_radius;
        double best;
        uint32_t j;
        if (hi >= self->size) {
            hi = self->size - 1u;
        }
        best = self->sums[lo];
        for (j = lo + 1u; j <= hi; ++j) {
            if (self->sums[j] > best) {
                best = self->sums[j];
            }
        }
        self->pooled[i] = best;
    }

    for (taken = 0u; taken < needed; ++taken) {
        uint32_t best = evictable_end; /* sentinel: nothing chosen yet */
        for (i = 0u; i < evictable_end; ++i) {
            if (self->doomed[i] != 0u) {
                continue;
            }
            /* Strictly less, so a tie leaves the earlier index standing — the
             * lower position, since the array is ascending. */
            if (best == evictable_end || self->pooled[i] < self->pooled[best]) {
                best = i;
            }
        }
        if (best == evictable_end) {
            break;
        }
        self->doomed[best] = 1u;
    }

    /* One compacting pass: victims come out in ascending position order, and
     * each survivor's whole history moves with it. */
    for (read = 0u; read < self->size; ++read) {
        if (self->doomed[read] != 0u) {
            self->doomed[read] = 0u;
            victims[written] = self->positions[read];
            written += 1;
            continue;
        }
        if (write != read) {
            self->positions[write] = self->positions[read];
            memmove(&self->history[(size_t)write * (size_t)self->obs_window],
                    &self->history[(size_t)read * (size_t)self->obs_window],
                    (size_t)self->obs_window * sizeof(float));
        }
        write += 1u;
    }
    self->size = write;
    return written;
}

static size_t snapkv_memory_bytes(const pb_kvcache *policy)
{
    const pb_kvcache_snapkv_state *self = (const pb_kvcache_snapkv_state *)policy;
    size_t slots;

    if (self == NULL) {
        return sizeof(pb_kvcache_snapkv_state);
    }
    slots = (size_t)self->capacity;
    return sizeof(pb_kvcache_snapkv_state) + slots * sizeof(uint32_t) +
           slots * (size_t)self->obs_window * sizeof(float) + slots * sizeof(double) +
           slots * sizeof(double) + slots * sizeof(uint8_t);
}

const pb_kvcache_vtable pb_kvcache_snapkv = { snapkv_create, snapkv_on_decode_step,
                                              snapkv_evict, snapkv_memory_bytes,
                                              snapkv_destroy };

/* ========================================================================
 * src/kv_cache/streaming_llm.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/streaming-llm/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * StreamingLLM — a sliding window that also pins the first few tokens.
 *
 * Mirrors index.ts and policy.py. The sliding window's ring buffer with the
 * sinks held outside it: a position below `sinks` is pinned and never enters
 * the ring, so tracking them costs a counter rather than storage.
 */




typedef struct pb_kvcache_streaming_llm_state {
    const pb_allocator *allocator;
    uint32_t sinks;      /* positions below this are pinned */
    uint32_t sinks_held; /* how many of them have been seen */
    uint32_t capacity;   /* budget - sinks + 1 */
    uint32_t *slots;     /* the recency window, oldest at head */
    uint32_t head;
    uint32_t size;
} pb_kvcache_streaming_llm_state;

static pb_kvcache *streaming_llm_create(const void *params, const pb_allocator *allocator,
                                        pb_rng *rng)
{
    const pb_kvcache_streaming_llm_params *config =
        (const pb_kvcache_streaming_llm_params *)params;
    pb_kvcache_streaming_llm_state *self;
    uint32_t budget;
    uint32_t sinks;
    uint32_t capacity;

    (void)rng; /* entirely deterministic */

    budget = (config == NULL) ? 512u : config->budget;
    sinks = (config == NULL) ? 4u : config->sinks;
    /* With zero sinks the ring holds budget + 1 slots; a budget of UINT32_MAX
     * would wrap it. */
    if (budget == 0u || budget == UINT32_MAX) {
        return NULL;
    }
    /* With no room left over the policy could not keep the newest token, which
     * would behave nothing like the paper. */
    if (sinks >= budget) {
        return NULL;
    }
    capacity = budget - sinks + 1u;

    self = (pb_kvcache_streaming_llm_state *)pb_alloc(
        allocator, sizeof(pb_kvcache_streaming_llm_state));
    if (self == NULL) {
        return NULL;
    }

    self->slots = (uint32_t *)pb_alloc(allocator, (size_t)capacity * sizeof(uint32_t));
    if (self->slots == NULL) {
        pb_free(allocator, self, sizeof(pb_kvcache_streaming_llm_state));
        return NULL;
    }

    self->allocator = allocator;
    self->sinks = sinks;
    self->capacity = capacity;
    self->head = 0u;
    self->size = 0u;
    self->sinks_held = 0u;

    /* Position 0's token exists before the first decode step (see kv_cache.h).
     * It is a sink when any are configured, and otherwise the first window
     * entry. */
    if (sinks > 0u) {
        self->sinks_held = 1u;
    } else {
        self->slots[0] = 0u;
        self->size = 1u;
    }
    return (pb_kvcache *)self;
}

static void streaming_llm_destroy(pb_kvcache *policy)
{
    pb_kvcache_streaming_llm_state *self = (pb_kvcache_streaming_llm_state *)policy;

    if (self == NULL) {
        return;
    }
    pb_free(self->allocator, self->slots, (size_t)self->capacity * sizeof(uint32_t));
    pb_free(self->allocator, self, sizeof(pb_kvcache_streaming_llm_state));
}

static void streaming_llm_on_decode_step(pb_kvcache *policy, uint32_t pos, const float *attn,
                                         size_t attn_len)
{
    pb_kvcache_streaming_llm_state *self = (pb_kvcache_streaming_llm_state *)policy;
    uint32_t slot;

    assert(self != NULL);

    /* This policy pins the structurally special positions, not the important
     * ones, so it never looks at a weight. */
    (void)attn;
    (void)attn_len;

    if (pos < self->sinks) {
        self->sinks_held += 1u;
        return;
    }

    /* Holding more than the window capacity means the caller's budget does not
     * match the policy's, which is a configuration error. */
    assert(self->size < self->capacity);
    if (self->size >= self->capacity) {
        return;
    }

    slot = self->head + self->size;
    if (slot >= self->capacity) {
        slot -= self->capacity;
    }
    self->slots[slot] = pos;
    self->size += 1u;
}

static size_t streaming_llm_evict(pb_kvcache *policy, uint32_t budget, uint32_t *victims,
                                  size_t capacity)
{
    pb_kvcache_streaming_llm_state *self = (pb_kvcache_streaming_llm_state *)policy;
    uint32_t held;
    uint32_t needed;
    size_t written = 0;

    assert(self != NULL);
    assert(victims != NULL);

    held = self->sinks_held + self->size;
    if (held <= budget) {
        return 0;
    }

    /* The sinks are never evicted, so at most the whole window can go — which
     * is why a budget below the sink count cannot be met and is not pretended
     * to be. */
    needed = held - budget;
    if (needed > self->size) {
        needed = self->size;
    }
    if ((size_t)needed > capacity) {
        return 0;
    }

    while (written < (size_t)needed) {
        victims[written] = self->slots[self->head];
        written += 1;
        self->head += 1u;
        if (self->head == self->capacity) {
            self->head = 0u;
        }
        self->size -= 1u;
    }
    return written;
}

static size_t streaming_llm_memory_bytes(const pb_kvcache *policy)
{
    const pb_kvcache_streaming_llm_state *self =
        (const pb_kvcache_streaming_llm_state *)policy;

    if (self == NULL) {
        return sizeof(pb_kvcache_streaming_llm_state);
    }
    return sizeof(pb_kvcache_streaming_llm_state) +
           (size_t)self->capacity * sizeof(uint32_t);
}

const pb_kvcache_vtable pb_kvcache_streaming_llm = { streaming_llm_create,
                                                     streaming_llm_on_decode_step,
                                                     streaming_llm_evict,
                                                     streaming_llm_memory_bytes,
                                                     streaming_llm_destroy };

/* ========================================================================
 * src/kv_cache/tova.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/tova/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * TOVA — drop whichever token the model just stopped looking at.
 *
 * Mirrors index.ts and policy.py. Structurally h2o with assignment in place of
 * accumulation and no recent window, which is the whole of the difference.
 */




/* Marks a position that has never appeared in an attention vector. Attention
 * weights are non-negative, so a negative value is unambiguous. */
#define PB_KVCACHE_TOVA_UNOBSERVED (-1.0)

typedef struct pb_kvcache_tova_state {
    const pb_allocator *allocator;
    uint32_t capacity; /* budget + 1 */
    uint32_t size;
    uint32_t *positions; /* kept positions, ascending */
    double *last_attn;   /* latest attention, parallel to positions */
    uint8_t *doomed;     /* eviction scratch, so evict allocates nothing */
} pb_kvcache_tova_state;

static void tova_release(pb_kvcache_tova_state *self)
{
    size_t slots;

    if (self == NULL) {
        return;
    }
    slots = (size_t)self->capacity;
    pb_free(self->allocator, self->positions, slots * sizeof(uint32_t));
    pb_free(self->allocator, self->last_attn, slots * sizeof(double));
    pb_free(self->allocator, self->doomed, slots * sizeof(uint8_t));
    pb_free(self->allocator, self, sizeof(pb_kvcache_tova_state));
}

static pb_kvcache *tova_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_kvcache_tova_params *config = (const pb_kvcache_tova_params *)params;
    pb_kvcache_tova_state *self;
    uint32_t budget;
    size_t slots;

    (void)rng; /* entirely deterministic */

    budget = (config == NULL) ? 512u : config->budget;
    /* The slot arrays hold budget + 1 entries; UINT32_MAX would wrap that. */
    if (budget == 0u || budget == UINT32_MAX) {
        return NULL;
    }

    self = (pb_kvcache_tova_state *)pb_alloc(allocator, sizeof(pb_kvcache_tova_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = budget + 1u;
    self->size = 0u;
    slots = (size_t)self->capacity;

    self->positions = (uint32_t *)pb_alloc(allocator, slots * sizeof(uint32_t));
    self->last_attn = (double *)pb_alloc(allocator, slots * sizeof(double));
    self->doomed = (uint8_t *)pb_alloc(allocator, slots * sizeof(uint8_t));
    if (self->positions == NULL || self->last_attn == NULL || self->doomed == NULL) {
        tova_release(self);
        return NULL;
    }

    /* Position 0's token exists before the first decode step (see kv_cache.h)
     * and nothing has attended to it yet. */
    self->positions[0] = 0u;
    self->last_attn[0] = PB_KVCACHE_TOVA_UNOBSERVED;
    self->doomed[0] = 0u;
    self->size = 1u;
    return (pb_kvcache *)self;
}

static void tova_destroy(pb_kvcache *policy)
{
    tova_release((pb_kvcache_tova_state *)policy);
}

static void tova_on_decode_step(pb_kvcache *policy, uint32_t pos, const float *attn,
                                size_t attn_len)
{
    pb_kvcache_tova_state *self = (pb_kvcache_tova_state *)policy;
    size_t shared;
    size_t i;

    assert(self != NULL);

    /* Assignment, not accumulation: the previous value is discarded outright,
     * which is the entire policy. */
    if (attn != NULL) {
        shared = (attn_len < (size_t)self->size) ? attn_len : (size_t)self->size;
        for (i = 0; i < shared; ++i) {
            self->last_attn[i] = (double)attn[i];
        }
    }

    assert(self->size < self->capacity);
    if (self->size >= self->capacity) {
        return;
    }

    self->positions[self->size] = pos;
    self->last_attn[self->size] = PB_KVCACHE_TOVA_UNOBSERVED;
    self->doomed[self->size] = 0u;
    self->size += 1u;
}

static size_t tova_evict(pb_kvcache *policy, uint32_t budget, uint32_t *victims, size_t capacity)
{
    pb_kvcache_tova_state *self = (pb_kvcache_tova_state *)policy;
    uint32_t needed;
    uint32_t taken;
    uint32_t read;
    uint32_t write = 0u;
    size_t written = 0;

    assert(self != NULL);
    assert(victims != NULL);

    if (self->size <= budget) {
        return 0;
    }
    needed = self->size - budget;

    /* Refusing outright rather than evicting as much as fits: a partial
     * eviction would leave the caller over budget with no way to tell that the
     * buffer, not the policy, was the limit. */
    if ((size_t)needed > capacity) {
        return 0;
    }

    for (taken = 0u; taken < needed; ++taken) {
        uint32_t best = self->size; /* sentinel: nothing chosen yet */
        uint32_t i;
        for (i = 0u; i < self->size; ++i) {
            if (self->doomed[i] != 0u) {
                continue;
            }
            /* An unobserved position is not a candidate: it has no weight to be
             * ranked on, and treating its absence as zero would evict every
             * token the step it was generated. */
            if (self->last_attn[i] == PB_KVCACHE_TOVA_UNOBSERVED) {
                continue;
            }
            /* Strictly less, so a tie leaves the earlier index standing — the
             * lower position, since the array is ascending. */
            if (best == self->size || self->last_attn[i] < self->last_attn[best]) {
                best = i;
            }
        }
        if (best == self->size) {
            break;
        }
        self->doomed[best] = 1u;
    }

    /* One compacting pass: victims come out in ascending position order. */
    for (read = 0u; read < self->size; ++read) {
        if (self->doomed[read] != 0u) {
            self->doomed[read] = 0u;
            victims[written] = self->positions[read];
            written += 1;
            continue;
        }
        self->positions[write] = self->positions[read];
        self->last_attn[write] = self->last_attn[read];
        write += 1u;
    }
    self->size = write;
    return written;
}

static size_t tova_memory_bytes(const pb_kvcache *policy)
{
    const pb_kvcache_tova_state *self = (const pb_kvcache_tova_state *)policy;
    size_t slots;

    if (self == NULL) {
        return sizeof(pb_kvcache_tova_state);
    }
    slots = (size_t)self->capacity;
    return sizeof(pb_kvcache_tova_state) + slots * sizeof(uint32_t) + slots * sizeof(double) +
           slots * sizeof(uint8_t);
}

const pb_kvcache_vtable pb_kvcache_tova = { tova_create, tova_on_decode_step, tova_evict,
                                            tova_memory_bytes, tova_destroy };

/* ========================================================================
 * src/kv_cache/traces.c
 * ======================================================================== */

/*
 * Mass assigned to each component. They sum to one.
 *
 * These are the same literals the TypeScript and Python generators use. Written
 * as decimal constants rather than fractions so all three parse the identical
 * float64 value.
 */
#define PB_KVCACHE_SINK_MASS 0.15
#define PB_KVCACHE_LOCAL_MASS 0.55
#define PB_KVCACHE_HEAVY_MASS 0.25
#define PB_KVCACHE_NOISE_MASS 0.05

/* The recency window: offsets 1..64 back from the current position. */
#define PB_KVCACHE_LOCAL_SPAN 64u
/* Offset d gets weight LOCAL_DECAY - d, so 1 is heaviest and 64 lightest. */
#define PB_KVCACHE_LOCAL_DECAY 65u

/*
 * Heavy hitters are drawn from [4, t - HEAVY_MARGIN]: clear of the sinks below
 * and of the recency window above, so the three components stay separable.
 */
#define PB_KVCACHE_HEAVY_MARGIN 65u
/* Before this step the heavy component is folded into the local one. */
#define PB_KVCACHE_HEAVY_START 128u
/* The heavy set is redrawn at every multiple of this. */
#define PB_KVCACHE_HEAVY_PERIOD 512u

/* How the sink mass is split across positions 0 to 3. Sums to SINK_MASS. */
static const double pb_kvcache_sink_weights[4] = { 0.06, 0.045, 0.03, 0.015 };

const pb_kvcache_trace_spec pb_kvcache_traces[] = {
    { "decode-4096", 4096u, 7u }
};

const pb_kvcache_trace_spec *pb_kvcache_trace_find(const char *id)
{
    size_t i;

    if (id == NULL) {
        return NULL;
    }
    for (i = 0; i < (size_t)PB_KVCACHE_TRACE_COUNT; ++i) {
        if (strcmp(pb_kvcache_traces[i].id, id) == 0) {
            return &pb_kvcache_traces[i];
        }
    }
    return NULL;
}

int pb_kvcache_trace_gen_init(pb_kvcache_trace_gen *gen, const pb_kvcache_trace_spec *spec,
                              const pb_allocator *allocator)
{
    size_t n;

    if (gen == NULL || spec == NULL) {
        return -1;
    }

    memset(gen, 0, sizeof(*gen));
    gen->spec = spec;
    gen->allocator = allocator;
    gen->step = 0u;
    gen->heavy_len = 0u;
    pb_rng_init(&gen->rng, spec->seed);

    n = (size_t)spec->sequence_length;
    gen->drawn = (uint8_t *)pb_alloc(allocator, n * sizeof(uint8_t));
    gen->scratch = (double *)pb_alloc(allocator, n * sizeof(double));
    gen->weights = (float *)pb_alloc(allocator, n * sizeof(float));

    if (gen->drawn == NULL || gen->scratch == NULL || gen->weights == NULL) {
        pb_kvcache_trace_gen_destroy(gen);
        return -1;
    }
    memset(gen->drawn, 0, n * sizeof(uint8_t));
    return 0;
}

void pb_kvcache_trace_gen_destroy(pb_kvcache_trace_gen *gen)
{
    size_t n;

    if (gen == NULL || gen->spec == NULL) {
        return;
    }

    n = (size_t)gen->spec->sequence_length;
    pb_free(gen->allocator, gen->drawn, n * sizeof(uint8_t));
    pb_free(gen->allocator, gen->scratch, n * sizeof(double));
    pb_free(gen->allocator, gen->weights, n * sizeof(float));
    gen->drawn = NULL;
    gen->scratch = NULL;
    gen->weights = NULL;
    gen->spec = NULL;
}

/*
 * Draw a fresh set of heavy-hitter positions.
 *
 * Rejection sampling with a pinned call order: draw, and if the position is
 * already in the set, draw again. Both the number of draws and their order
 * matter, because the rank of a position in the set decides its weight — which
 * is why this mirrors the reference's loop exactly rather than using a cheaper
 * shuffle that would consume the stream differently.
 *
 * Membership is a flag array rather than a hash set. It is cleared by walking
 * the positions just drawn, so the cost is the set size and not the sequence
 * length.
 */
static void pb_kvcache_draw_heavy(pb_kvcache_trace_gen *gen, uint32_t t)
{
    uint32_t span;
    size_t i;

    for (i = 0; i < gen->heavy_len; ++i) {
        gen->drawn[gen->heavy[i]] = 0u;
    }
    gen->heavy_len = 0u;

    /*
     * Positions run from 4 (clear of the sinks) to t - HEAVY_MARGIN (just
     * outside the recency window, which at step t covers t-64 .. t-1), so the
     * draw is next_int(span) + 4 over that many candidates.
     */
    if (t < PB_KVCACHE_HEAVY_MARGIN + 4u) {
        return;
    }
    span = t - PB_KVCACHE_HEAVY_MARGIN - 4u + 1u;
    if (span < PB_KVCACHE_HEAVY_COUNT) {
        return;
    }

    while (gen->heavy_len < (size_t)PB_KVCACHE_HEAVY_COUNT) {
        uint32_t position = pb_rng_next_int(&gen->rng, span) + 4u;
        if (gen->drawn[position] != 0u) {
            continue;
        }
        gen->drawn[position] = 1u;
        gen->heavy[gen->heavy_len] = position;
        gen->heavy_len += 1u;
    }
}

/*
 * The attention weights for one decode step, over positions 0 .. t-1.
 *
 * Contributions accumulate in a fixed order — sink, local, heavy, noise —
 * because float addition is not associative and a different order would give a
 * different last bit. Then one division per position normalises, and one cast
 * to float rounds.
 */
static void pb_kvcache_step_weights(pb_kvcache_trace_gen *gen, uint32_t t)
{
    double *out = gen->scratch;
    uint32_t sinks;
    uint32_t span;
    uint32_t d;
    uint32_t i;
    size_t rank;
    double sink_total = 0.0;
    double local_weight = 0.0;
    double local_mass;
    double per_position;
    double total = 0.0;

    memset(out, 0, (size_t)t * sizeof(double));

    /*
     * Sinks. When fewer than four positions exist the available weights are
     * scaled so the component still contributes exactly SINK_MASS.
     */
    sinks = (t < 4u) ? t : 4u;
    for (i = 0; i < sinks; ++i) {
        sink_total += pb_kvcache_sink_weights[i];
    }
    for (i = 0; i < sinks; ++i) {
        out[i] = out[i] + (pb_kvcache_sink_weights[i] * PB_KVCACHE_SINK_MASS) / sink_total;
    }

    /* Local: offsets 1..min(64, t) back from t, weighted toward the recent. */
    span = (t < PB_KVCACHE_LOCAL_SPAN) ? t : PB_KVCACHE_LOCAL_SPAN;
    for (d = 1u; d <= span; ++d) {
        local_weight += (double)(PB_KVCACHE_LOCAL_DECAY - d);
    }

    /*
     * Below HEAVY_START the heavy component has nowhere to live, so its mass
     * joins the local one and the total still comes to one.
     */
    local_mass = (t < PB_KVCACHE_HEAVY_START)
                     ? PB_KVCACHE_LOCAL_MASS + PB_KVCACHE_HEAVY_MASS
                     : PB_KVCACHE_LOCAL_MASS;
    for (d = 1u; d <= span; ++d) {
        uint32_t position = t - d;
        out[position] =
            out[position] + ((double)(PB_KVCACHE_LOCAL_DECAY - d) * local_mass) / local_weight;
    }

    /* Heavy hitters, weighted by draw order: the first drawn is heaviest. */
    if (t >= PB_KVCACHE_HEAVY_START && gen->heavy_len > 0u) {
        double heavy_weight = 0.0;
        for (rank = 0; rank < gen->heavy_len; ++rank) {
            heavy_weight += 1.0 / (double)(rank + 1u);
        }
        for (rank = 0; rank < gen->heavy_len; ++rank) {
            uint32_t position = gen->heavy[rank];
            out[position] =
                out[position] + (PB_KVCACHE_HEAVY_MASS / (double)(rank + 1u)) / heavy_weight;
        }
    }

    /* Noise, so no position is ever exactly zero. */
    per_position = PB_KVCACHE_NOISE_MASS / (double)t;
    for (i = 0; i < t; ++i) {
        out[i] = out[i] + per_position;
    }

    /*
     * Normalise, then take each weight to float32. The total is one to within
     * rounding already, and removing this division was measured to change no
     * output bit — it stays because it makes the invariant exact in double too.
     */
    for (i = 0; i < t; ++i) {
        total += out[i];
    }
    for (i = 0; i < t; ++i) {
        gen->weights[i] = (float)(out[i] / total);
    }
}

const float *pb_kvcache_trace_gen_next(pb_kvcache_trace_gen *gen, size_t *len)
{
    uint32_t t;

    if (gen == NULL || gen->spec == NULL || gen->weights == NULL) {
        return NULL;
    }

    t = gen->step + 1u;
    /* The last step is sequence_length - 1: position 0's token never attends. */
    if (t >= gen->spec->sequence_length) {
        return NULL;
    }

    /*
     * The set is drawn when first needed and redrawn on the period. Both
     * conditions are pinned: a port that redrew on a different step would
     * diverge from here on, and the parity test would say exactly where.
     */
    if (t == PB_KVCACHE_HEAVY_START ||
        (t > PB_KVCACHE_HEAVY_START && t % PB_KVCACHE_HEAVY_PERIOD == 0u)) {
        pb_kvcache_draw_heavy(gen, t);
    }

    pb_kvcache_step_weights(gen, t);
    gen->step = t;
    if (len != NULL) {
        *len = (size_t)t;
    }
    return gen->weights;
}

#define PB_KVCACHE_FNV_OFFSET_BASIS 0x811C9DC5u
#define PB_KVCACHE_FNV_PRIME 0x01000193u

uint32_t pb_kvcache_trace_hash(const pb_kvcache_trace_spec *spec, const pb_allocator *allocator)
{
    pb_kvcache_trace_gen gen;
    uint32_t digest = PB_KVCACHE_FNV_OFFSET_BASIS;
    const float *weights;
    size_t len;

    if (pb_kvcache_trace_gen_init(&gen, spec, allocator) != 0) {
        return 0u;
    }

    while ((weights = pb_kvcache_trace_gen_next(&gen, &len)) != NULL) {
        size_t i;
        for (i = 0; i < len; ++i) {
            uint32_t bits;
            unsigned byte;
            /*
             * memcpy rather than a pointer cast: reading a float through a
             * uint32_t lvalue is undefined behaviour, and UBSan is watching.
             * Every compiler here turns this into a register move.
             */
            memcpy(&bits, &weights[i], sizeof(bits));
            /*
             * Little-endian byte order regardless of the host's, so a
             * big-endian machine hashes the same bytes in the same order.
             */
            for (byte = 0u; byte < 4u; ++byte) {
                digest ^= (bits >> (byte * 8u)) & 0xFFu;
                digest *= PB_KVCACHE_FNV_PRIME;
            }
        }
    }

    pb_kvcache_trace_gen_destroy(&gen);
    return digest;
}

/* ========================================================================
 * src/rate_limiter/dual_bucket.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/dual-bucket/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * DualBucket — two limits at once, and a request must satisfy both.
 *
 * Mirrors index.ts and policy.py. A map from key to slot, and five parallel
 * arrays: two balances, two carries, and when the slot was last brought up to
 * date.
 */




/* Both ceilings are stated per minute, so the ledger's period is a minute. */
#define PB_DUAL_PERIOD_MS 60000u

typedef struct pb_ratelimiter_dual_bucket_state {
    const pb_allocator *allocator;
    pb_map index; /* key -> slot */
    uint32_t *requests;
    uint32_t *request_credit;
    uint32_t *tokens;
    uint32_t *token_credit;
    uint64_t *last;
    uint32_t requests_per_min;
    uint32_t tokens_per_min;
    uint32_t max_keys;
    uint32_t used;
} pb_ratelimiter_dual_bucket_state;

/*
 * Advance one dimension's ledger.
 *
 * The token bucket's integer ledger with a period of a minute instead of a
 * second: the fraction lives in a credit measured in PB_DUAL_PERIOD_MS-ths of a
 * permit, so nothing is ever rounded away.
 */
static void dual_bucket_advance(uint32_t *balance, uint32_t *credit, uint32_t rate_per_min,
                    uint64_t elapsed)
{
    uint64_t accrued = (uint64_t)*credit + (uint64_t)rate_per_min * elapsed;
    uint64_t whole = (uint64_t)*balance + accrued / PB_DUAL_PERIOD_MS;

    accrued %= PB_DUAL_PERIOD_MS;
    if (whole >= (uint64_t)rate_per_min) {
        whole = (uint64_t)rate_per_min;
        accrued = 0u;
    }

    *balance = (uint32_t)whole;
    *credit = (uint32_t)accrued;
}

/*
 * Bring both ledgers up to date at `now_ms`.
 *
 * Elapsed time is clamped to one period, which is exactly how long a drained
 * bucket takes to dual_bucket_refill — beyond that the result cannot change, and clamping
 * keeps the multiply well inside 64 bits.
 */
static void dual_bucket_refill(pb_ratelimiter_dual_bucket_state *self, uint32_t slot, uint64_t now_ms)
{
    uint64_t elapsed;

    if (now_ms <= self->last[slot]) {
        return;
    }
    elapsed = now_ms - self->last[slot];
    if (elapsed > (uint64_t)PB_DUAL_PERIOD_MS) {
        elapsed = (uint64_t)PB_DUAL_PERIOD_MS;
    }

    dual_bucket_advance(&self->requests[slot], &self->request_credit[slot], self->requests_per_min, elapsed);
    dual_bucket_advance(&self->tokens[slot], &self->token_credit[slot], self->tokens_per_min, elapsed);
    self->last[slot] = now_ms;
}

/*
 * Find a key's slot, claiming a free one if it has never been seen.
 *
 * Returns false when the table is full, which is the fail-closed case: a new
 * key is refused rather than being silently let through.
 */
static bool dual_bucket_slot_for(pb_ratelimiter_dual_bucket_state *self, uint64_t key, uint64_t now_ms,
                     uint32_t *slot)
{
    if (pb_map_get(&self->index, key, slot)) {
        dual_bucket_refill(self, *slot, now_ms);
        return true;
    }
    if (self->used >= self->max_keys) {
        return false;
    }
    *slot = self->used;
    if (!pb_map_put(&self->index, key, *slot)) {
        return false;
    }
    self->used += 1u;
    self->requests[*slot] = self->requests_per_min;
    self->request_credit[*slot] = 0u;
    self->tokens[*slot] = self->tokens_per_min;
    self->token_credit[*slot] = 0u;
    self->last[*slot] = now_ms;
    return true;
}

static pb_ratelimiter *dual_bucket_create(const void *params, const pb_allocator *allocator,
                                          pb_rng *rng)
{
    const pb_ratelimiter_dual_bucket_params *config =
        (const pb_ratelimiter_dual_bucket_params *)params;
    pb_ratelimiter_dual_bucket_state *self;
    uint32_t requests_per_min;
    uint32_t tokens_per_min;
    uint32_t max_keys;
    bool ok;

    (void)rng; /* a dual bucket makes no random choices */

    requests_per_min = (config == NULL) ? 500u : config->requests_per_min;
    tokens_per_min = (config == NULL) ? 200000u : config->tokens_per_min;
    max_keys = (config == NULL) ? 1024u : config->max_keys;
    if (requests_per_min == 0u || tokens_per_min == 0u || max_keys == 0u) {
        return NULL;
    }

    self = (pb_ratelimiter_dual_bucket_state *)pb_alloc(
        allocator, sizeof(pb_ratelimiter_dual_bucket_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->requests_per_min = requests_per_min;
    self->tokens_per_min = tokens_per_min;
    self->max_keys = max_keys;
    self->used = 0u;
    self->requests = NULL;
    self->request_credit = NULL;
    self->tokens = NULL;
    self->token_credit = NULL;
    self->last = NULL;

    if (!pb_map_init(&self->index, max_keys, allocator)) {
        pb_free(allocator, self, sizeof(pb_ratelimiter_dual_bucket_state));
        return NULL;
    }

    self->requests = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->request_credit = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->tokens = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->token_credit = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->last = (uint64_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint64_t));

    ok = self->requests != NULL && self->request_credit != NULL && self->tokens != NULL &&
         self->token_credit != NULL && self->last != NULL;
    if (!ok) {
        pb_free(allocator, self->requests, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->request_credit, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->tokens, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->token_credit, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->last, (size_t)max_keys * sizeof(uint64_t));
        pb_map_destroy(&self->index, allocator);
        pb_free(allocator, self, sizeof(pb_ratelimiter_dual_bucket_state));
        return NULL;
    }

    return (pb_ratelimiter *)self;
}

static void dual_bucket_destroy(pb_ratelimiter *limiter)
{
    pb_ratelimiter_dual_bucket_state *self = (pb_ratelimiter_dual_bucket_state *)limiter;
    const pb_allocator *allocator;
    size_t words;
    size_t stamps;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    words = (size_t)self->max_keys * sizeof(uint32_t);
    stamps = (size_t)self->max_keys * sizeof(uint64_t);

    pb_free(allocator, self->requests, words);
    pb_free(allocator, self->request_credit, words);
    pb_free(allocator, self->tokens, words);
    pb_free(allocator, self->token_credit, words);
    pb_free(allocator, self->last, stamps);
    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self, sizeof(pb_ratelimiter_dual_bucket_state));
}

static bool dual_bucket_allow(pb_ratelimiter *limiter, uint64_t key, uint32_t cost,
                              uint64_t now_ms)
{
    pb_ratelimiter_dual_bucket_state *self = (pb_ratelimiter_dual_bucket_state *)limiter;
    uint32_t slot;

    assert(self != NULL);

    if (!dual_bucket_slot_for(self, key, now_ms, &slot)) {
        return false;
    }

    /* Both dimensions are tested before either is charged. A caller refused for
     * work must not have quietly spent a request too, or retrying would
     * throttle it harder than not retrying. */
    if (self->requests[slot] < 1u) {
        return false;
    }
    if (self->tokens[slot] < cost) {
        return false;
    }

    self->requests[slot] -= 1u;
    self->tokens[slot] -= cost;
    return true;
}

/* Milliseconds until one whole permit accrues on a dimension. */
static uint64_t wait_for(uint32_t available, uint32_t credit, uint32_t rate_per_min)
{
    uint64_t deficit;

    if (available >= 1u) {
        return 0u;
    }
    deficit = (uint64_t)PB_DUAL_PERIOD_MS - (uint64_t)credit;
    return (deficit + (uint64_t)rate_per_min - 1u) / (uint64_t)rate_per_min;
}

static uint64_t dual_bucket_retry_after(pb_ratelimiter *limiter, uint64_t key, uint64_t now_ms)
{
    pb_ratelimiter_dual_bucket_state *self = (pb_ratelimiter_dual_bucket_state *)limiter;
    uint64_t by_requests;
    uint64_t by_tokens;
    uint32_t slot;

    assert(self != NULL);

    if (!pb_map_get(&self->index, key, &slot)) {
        /* Untracked. With room in the table this key would be admitted right
         * now, so zero is the truth; with the table full it will never be
         * admitted, and zero would be a lie a caller acts on. */
        return self->used >= self->max_keys ? PB_RATELIMITER_RETRY_UNKNOWN : 0u;
    }
    dual_bucket_refill(self, slot, now_ms);

    by_requests =
        wait_for(self->requests[slot], self->request_credit[slot], self->requests_per_min);
    by_tokens = wait_for(self->tokens[slot], self->token_credit[slot], self->tokens_per_min);
    return by_requests > by_tokens ? by_requests : by_tokens;
}

static size_t dual_bucket_state_size(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_dual_bucket_state *self =
        (const pb_ratelimiter_dual_bucket_state *)limiter;
    assert(self != NULL);
    return (size_t)self->used;
}

static size_t dual_bucket_memory_bytes(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_dual_bucket_state *self =
        (const pb_ratelimiter_dual_bucket_state *)limiter;
    assert(self != NULL);
    return sizeof(pb_ratelimiter_dual_bucket_state) +
           (size_t)self->max_keys * (4u * sizeof(uint32_t) + sizeof(uint64_t)) +
           pb_map_memory_bytes(&self->index);
}

const pb_ratelimiter_vtable pb_ratelimiter_dual_bucket = { dual_bucket_create,
                                                           dual_bucket_allow,
                                                           dual_bucket_retry_after,
                                                           dual_bucket_state_size,
                                                           dual_bucket_memory_bytes,
                                                           dual_bucket_destroy };

/* ========================================================================
 * src/rate_limiter/fixed_window.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/fixed-window/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * FixedWindow — count requests inside a clock-aligned window, reset at the edge.
 *
 * Mirrors index.ts and policy.py. A map from key to slot, and two parallel
 * arrays holding the window each slot's count belongs to and the count itself.
 */




typedef struct pb_ratelimiter_fixed_window_state {
    const pb_allocator *allocator;
    pb_map index; /* key -> slot */
    uint64_t *window_start;
    uint32_t *count;
    uint32_t limit;
    uint32_t window_ms;
    uint32_t max_keys;
    uint32_t used;
} pb_ratelimiter_fixed_window_state;

/* The start of the window containing `now`. Integer division, no floats. */
static uint64_t fixed_window_window_of(const pb_ratelimiter_fixed_window_state *self, uint64_t now)
{
    return now - (now % (uint64_t)self->window_ms);
}

/*
 * Find a key's slot, claiming a free one if it has never been seen.
 *
 * Returns false when the table is full, which is the fail-closed case: a new
 * key is refused rather than being silently let through.
 */
static bool fixed_window_slot_for(pb_ratelimiter_fixed_window_state *self, uint64_t key, uint32_t *slot)
{
    if (pb_map_get(&self->index, key, slot)) {
        return true;
    }
    if (self->used >= self->max_keys) {
        return false;
    }
    *slot = self->used;
    if (!pb_map_put(&self->index, key, *slot)) {
        return false;
    }
    self->used += 1u;
    self->window_start[*slot] = 0u;
    self->count[*slot] = 0u;
    return true;
}

static pb_ratelimiter *fixed_window_create(const void *params, const pb_allocator *allocator,
                                           pb_rng *rng)
{
    const pb_ratelimiter_fixed_window_params *config =
        (const pb_ratelimiter_fixed_window_params *)params;
    pb_ratelimiter_fixed_window_state *self;
    uint32_t limit;
    uint32_t window_ms;
    uint32_t max_keys;

    (void)rng; /* a fixed window makes no random choices */

    limit = (config == NULL) ? 100u : config->limit;
    window_ms = (config == NULL) ? 1000u : config->window_ms;
    max_keys = (config == NULL) ? 1024u : config->max_keys;
    if (limit == 0u || window_ms == 0u || max_keys == 0u) {
        return NULL;
    }

    self = (pb_ratelimiter_fixed_window_state *)pb_alloc(
        allocator, sizeof(pb_ratelimiter_fixed_window_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->limit = limit;
    self->window_ms = window_ms;
    self->max_keys = max_keys;
    self->used = 0u;
    self->window_start = NULL;
    self->count = NULL;

    if (!pb_map_init(&self->index, max_keys, allocator)) {
        pb_free(allocator, self, sizeof(pb_ratelimiter_fixed_window_state));
        return NULL;
    }

    self->window_start = (uint64_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint64_t));
    self->count = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    if (self->window_start == NULL || self->count == NULL) {
        pb_free(allocator, self->window_start, (size_t)max_keys * sizeof(uint64_t));
        pb_free(allocator, self->count, (size_t)max_keys * sizeof(uint32_t));
        pb_map_destroy(&self->index, allocator);
        pb_free(allocator, self, sizeof(pb_ratelimiter_fixed_window_state));
        return NULL;
    }

    return (pb_ratelimiter *)self;
}

static void fixed_window_destroy(pb_ratelimiter *limiter)
{
    pb_ratelimiter_fixed_window_state *self = (pb_ratelimiter_fixed_window_state *)limiter;
    const pb_allocator *allocator;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    pb_free(allocator, self->window_start, (size_t)self->max_keys * sizeof(uint64_t));
    pb_free(allocator, self->count, (size_t)self->max_keys * sizeof(uint32_t));
    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self, sizeof(pb_ratelimiter_fixed_window_state));
}

static bool fixed_window_allow(pb_ratelimiter *limiter, uint64_t key, uint32_t cost,
                               uint64_t now_ms)
{
    pb_ratelimiter_fixed_window_state *self = (pb_ratelimiter_fixed_window_state *)limiter;
    uint64_t start;
    uint32_t slot;

    assert(self != NULL);

    if (!fixed_window_slot_for(self, key, &slot)) {
        return false;
    }

    start = fixed_window_window_of(self, now_ms);
    if (self->window_start[slot] != start) {
        /* A new window: the old count is gone, however recently it was earned.
         * This discontinuity is the whole trade-off. A slot claimed for a key
         * never seen starts at window 0 with a count of 0, so it needs no
         * special case — either the window differs and it resets, or `now` is
         * genuinely in window 0 and the count is already right. */
        self->window_start[slot] = start;
        self->count[slot] = 0u;
    }

    if ((uint64_t)self->count[slot] + (uint64_t)cost > (uint64_t)self->limit) {
        return false;
    }
    self->count[slot] += cost;
    return true;
}

static uint64_t fixed_window_retry_after(pb_ratelimiter *limiter, uint64_t key, uint64_t now_ms)
{
    pb_ratelimiter_fixed_window_state *self = (pb_ratelimiter_fixed_window_state *)limiter;
    uint64_t start;
    uint32_t slot;

    assert(self != NULL);

    if (!pb_map_get(&self->index, key, &slot)) {
        /* Untracked. With room in the table this key would be admitted right
         * now, so zero is the truth; with the table full it will never be
         * admitted, and zero would be a lie a caller acts on. */
        return self->used >= self->max_keys ? PB_RATELIMITER_RETRY_UNKNOWN : 0u;
    }

    start = fixed_window_window_of(self, now_ms);
    if (self->window_start[slot] != start) {
        return 0u;
    }
    if (self->count[slot] < self->limit) {
        return 0u;
    }
    return start + (uint64_t)self->window_ms - now_ms;
}

static size_t fixed_window_state_size(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_fixed_window_state *self =
        (const pb_ratelimiter_fixed_window_state *)limiter;
    assert(self != NULL);
    return (size_t)self->used;
}

static size_t fixed_window_memory_bytes(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_fixed_window_state *self =
        (const pb_ratelimiter_fixed_window_state *)limiter;
    assert(self != NULL);
    return sizeof(pb_ratelimiter_fixed_window_state) +
           (size_t)self->max_keys * (sizeof(uint64_t) + sizeof(uint32_t)) +
           pb_map_memory_bytes(&self->index);
}

const pb_ratelimiter_vtable pb_ratelimiter_fixed_window = { fixed_window_create,
                                                            fixed_window_allow,
                                                            fixed_window_retry_after,
                                                            fixed_window_state_size,
                                                            fixed_window_memory_bytes,
                                                            fixed_window_destroy };

/* ========================================================================
 * src/rate_limiter/gcra.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/gcra/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * Gcra — the token bucket, kept as one number instead of three.
 *
 * Mirrors index.ts and policy.py. A map from key to slot and a single array of
 * theoretical arrival times — the whole state, and the reason to prefer this
 * over token_bucket.c when keys are many.
 *
 * The TAT is signed rather than unsigned. A key's schedule can legitimately sit
 * *behind* the clock (that is what an idle bucket looks like), and the
 * conformance test subtracts the tolerance from it, so intermediate values go
 * negative for any key seen early in a run. Unsigned arithmetic would wrap
 * those into enormous positives and refuse everything.
 */




/* Scaled units in one permit. One millisecond is `rate_per_sec` of them. */
#define PB_GCRA_UNIT 1000

typedef struct pb_ratelimiter_gcra_state {
    const pb_allocator *allocator;
    pb_map index; /* key -> slot */
    int64_t *tat; /* theoretical arrival time, in scaled units */
    int64_t tolerance;
    uint32_t rate_per_sec;
    uint32_t burst;
    uint32_t max_keys;
    uint32_t used;
} pb_ratelimiter_gcra_state;

/*
 * Find a key's slot, claiming a free one if it has never been seen.
 *
 * A fresh slot is seeded with the current scaled time, which is what "fully
 * conforming" means: the schedule starts now, so the whole burst is available.
 *
 * Returns false when the table is full, which is the fail-closed case: a new
 * key is refused rather than being silently let through.
 */
static bool gcra_slot_for(pb_ratelimiter_gcra_state *self, uint64_t key, int64_t scaled,
                     uint32_t *slot)
{
    if (pb_map_get(&self->index, key, slot)) {
        return true;
    }
    if (self->used >= self->max_keys) {
        return false;
    }
    *slot = self->used;
    if (!pb_map_put(&self->index, key, *slot)) {
        return false;
    }
    self->used += 1u;
    self->tat[*slot] = scaled;
    return true;
}

static pb_ratelimiter *gcra_create(const void *params, const pb_allocator *allocator,
                                   pb_rng *rng)
{
    const pb_ratelimiter_gcra_params *config = (const pb_ratelimiter_gcra_params *)params;
    pb_ratelimiter_gcra_state *self;
    uint32_t rate_per_sec;
    uint32_t burst;
    uint32_t max_keys;

    (void)rng; /* GCRA makes no random choices */

    rate_per_sec = (config == NULL) ? 100u : config->rate_per_sec;
    burst = (config == NULL) ? 100u : config->burst;
    max_keys = (config == NULL) ? 1024u : config->max_keys;
    if (rate_per_sec == 0u || burst == 0u || max_keys == 0u) {
        return NULL;
    }

    self = (pb_ratelimiter_gcra_state *)pb_alloc(allocator, sizeof(pb_ratelimiter_gcra_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->rate_per_sec = rate_per_sec;
    self->burst = burst;
    self->tolerance = (int64_t)(burst - 1u) * PB_GCRA_UNIT;
    self->max_keys = max_keys;
    self->used = 0u;
    self->tat = NULL;

    if (!pb_map_init(&self->index, max_keys, allocator)) {
        pb_free(allocator, self, sizeof(pb_ratelimiter_gcra_state));
        return NULL;
    }

    self->tat = (int64_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(int64_t));
    if (self->tat == NULL) {
        pb_map_destroy(&self->index, allocator);
        pb_free(allocator, self, sizeof(pb_ratelimiter_gcra_state));
        return NULL;
    }

    return (pb_ratelimiter *)self;
}

static void gcra_destroy(pb_ratelimiter *limiter)
{
    pb_ratelimiter_gcra_state *self = (pb_ratelimiter_gcra_state *)limiter;
    const pb_allocator *allocator;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    pb_free(allocator, self->tat, (size_t)self->max_keys * sizeof(int64_t));
    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self, sizeof(pb_ratelimiter_gcra_state));
}

static bool gcra_allow(pb_ratelimiter *limiter, uint64_t key, uint32_t cost, uint64_t now_ms)
{
    pb_ratelimiter_gcra_state *self = (pb_ratelimiter_gcra_state *)limiter;
    int64_t scaled;
    int64_t tat;
    uint32_t slot;

    assert(self != NULL);

    /* A cost above the burst can never be met. The conformance test below does
     * not cap on its own — an idle key's TAT sits arbitrarily far in the past —
     * so the ceiling has to be stated. */
    if (cost > self->burst) {
        return false;
    }

    scaled = (int64_t)now_ms * (int64_t)self->rate_per_sec;
    if (!gcra_slot_for(self, key, scaled, &slot)) {
        return false;
    }
    tat = self->tat[slot];

    /* Signed before the subtraction: at cost 0 the reference computes
     * (cost - 1) * UNIT = -UNIT, where `cost - 1u` would wrap to 2^32 - 1. */
    if (scaled < tat - self->tolerance + ((int64_t)cost - 1) * PB_GCRA_UNIT) {
        return false;
    }

    /* `max` is what stops an idle key banking unbounded credit: the schedule
     * restarts from now rather than from a TAT left far in the past. */
    self->tat[slot] = (scaled > tat ? scaled : tat) + (int64_t)cost * PB_GCRA_UNIT;
    return true;
}

static uint64_t gcra_retry_after(pb_ratelimiter *limiter, uint64_t key, uint64_t now_ms)
{
    pb_ratelimiter_gcra_state *self = (pb_ratelimiter_gcra_state *)limiter;
    int64_t scaled;
    int64_t target;
    int64_t at;
    uint32_t slot;

    assert(self != NULL);

    if (!pb_map_get(&self->index, key, &slot)) {
        /* Untracked. With room in the table this key would be admitted right
         * now, so zero is the truth; with the table full it will never be
         * admitted, and zero would be a lie a caller acts on. */
        return self->used >= self->max_keys ? PB_RATELIMITER_RETRY_UNKNOWN : 0u;
    }

    scaled = (int64_t)now_ms * (int64_t)self->rate_per_sec;
    target = self->tat[slot] - self->tolerance;
    if (scaled >= target) {
        return 0u;
    }

    /* The first whole millisecond at or past the target. `target` is positive
     * here, since it exceeds a non-negative `scaled`. */
    at = (target + (int64_t)self->rate_per_sec - 1) / (int64_t)self->rate_per_sec;
    return (uint64_t)(at - (int64_t)now_ms);
}

static size_t gcra_state_size(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_gcra_state *self = (const pb_ratelimiter_gcra_state *)limiter;
    assert(self != NULL);
    return (size_t)self->used;
}

static size_t gcra_memory_bytes(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_gcra_state *self = (const pb_ratelimiter_gcra_state *)limiter;
    assert(self != NULL);
    return sizeof(pb_ratelimiter_gcra_state) + (size_t)self->max_keys * sizeof(int64_t) +
           pb_map_memory_bytes(&self->index);
}

const pb_ratelimiter_vtable pb_ratelimiter_gcra = { gcra_create,     gcra_allow,
                                                    gcra_retry_after, gcra_state_size,
                                                    gcra_memory_bytes, gcra_destroy };

/* ========================================================================
 * src/rate_limiter/leaky_bucket.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/leaky-bucket/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * LeakyBucket — a level that rises with each request and drains at a steady rate.
 *
 * Mirrors index.ts and policy.py, and mirrors token_bucket.c line for line
 * under the substitution `tokens = capacity - level`. Keeping the two in step
 * is deliberate: a vector in each policy pins the equivalence, so a change to
 * one that is not made to the other fails a test rather than drifting quietly.
 */




typedef struct pb_ratelimiter_leaky_bucket_state {
    const pb_allocator *allocator;
    pb_map index;     /* key -> slot */
    uint32_t *level;  /* whole units currently in the bucket */
    uint32_t *credit; /* thousandths of a unit drained, always 0..999 */
    uint64_t *last;   /* when the slot was last brought up to date */
    uint32_t rate_per_sec;
    uint32_t capacity;
    uint32_t drain_ms; /* how long a full bucket takes to drain */
    uint32_t max_keys;
    uint32_t used;
} pb_ratelimiter_leaky_bucket_state;

/*
 * Let a bucket leak up to `now_ms`.
 *
 * Idle time is clamped to `drain_ms` before the multiply, so a key untouched
 * for a month cannot overflow the arithmetic. The result is identical either
 * way, because the bucket empties long before.
 */
static void drain(pb_ratelimiter_leaky_bucket_state *self, uint32_t slot, uint64_t now_ms)
{
    uint64_t elapsed;
    uint64_t credit;
    uint64_t drained;

    if (now_ms <= self->last[slot]) {
        return;
    }
    elapsed = now_ms - self->last[slot];
    if (elapsed > (uint64_t)self->drain_ms) {
        elapsed = (uint64_t)self->drain_ms;
    }

    credit = (uint64_t)self->credit[slot] + (uint64_t)self->rate_per_sec * elapsed;
    drained = credit / 1000u;
    credit %= 1000u;

    if (drained >= (uint64_t)self->level[slot]) {
        /* The bucket has run dry. Nothing further leaks, and the fraction that
         * would have leaked next is discarded. */
        self->level[slot] = 0u;
        credit = 0u;
    } else {
        self->level[slot] -= (uint32_t)drained;
    }

    self->credit[slot] = (uint32_t)credit;
    self->last[slot] = now_ms;
}

/*
 * Find a key's slot, claiming a free one if it has never been seen.
 *
 * Returns false when the table is full, which is the fail-closed case: a new
 * key is refused rather than being silently let through.
 */
static bool leaky_bucket_slot_for(pb_ratelimiter_leaky_bucket_state *self, uint64_t key, uint64_t now_ms,
                     uint32_t *slot)
{
    if (pb_map_get(&self->index, key, slot)) {
        drain(self, *slot, now_ms);
        return true;
    }
    if (self->used >= self->max_keys) {
        return false;
    }
    *slot = self->used;
    if (!pb_map_put(&self->index, key, *slot)) {
        return false;
    }
    self->used += 1u;
    /* A key never seen starts empty: it has been draining for all of history. */
    self->level[*slot] = 0u;
    self->credit[*slot] = 0u;
    self->last[*slot] = now_ms;
    return true;
}

static pb_ratelimiter *leaky_bucket_create(const void *params, const pb_allocator *allocator,
                                           pb_rng *rng)
{
    const pb_ratelimiter_leaky_bucket_params *config =
        (const pb_ratelimiter_leaky_bucket_params *)params;
    pb_ratelimiter_leaky_bucket_state *self;
    uint32_t rate_per_sec;
    uint32_t capacity;
    uint32_t max_keys;

    (void)rng; /* a leaky bucket makes no random choices */

    rate_per_sec = (config == NULL) ? 100u : config->rate_per_sec;
    capacity = (config == NULL) ? 1u : config->capacity;
    max_keys = (config == NULL) ? 1024u : config->max_keys;
    if (rate_per_sec == 0u || capacity == 0u || max_keys == 0u) {
        return NULL;
    }

    self = (pb_ratelimiter_leaky_bucket_state *)pb_alloc(
        allocator, sizeof(pb_ratelimiter_leaky_bucket_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->rate_per_sec = rate_per_sec;
    self->capacity = capacity;
    /* Ceiling division, in 64 bits so a large capacity cannot overflow. */
    self->drain_ms =
        (uint32_t)(((uint64_t)capacity * 1000u + (uint64_t)rate_per_sec - 1u) / rate_per_sec);
    self->max_keys = max_keys;
    self->used = 0u;
    self->level = NULL;
    self->credit = NULL;
    self->last = NULL;

    if (!pb_map_init(&self->index, max_keys, allocator)) {
        pb_free(allocator, self, sizeof(pb_ratelimiter_leaky_bucket_state));
        return NULL;
    }

    self->level = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->credit = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->last = (uint64_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint64_t));
    if (self->level == NULL || self->credit == NULL || self->last == NULL) {
        pb_free(allocator, self->level, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->credit, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->last, (size_t)max_keys * sizeof(uint64_t));
        pb_map_destroy(&self->index, allocator);
        pb_free(allocator, self, sizeof(pb_ratelimiter_leaky_bucket_state));
        return NULL;
    }

    return (pb_ratelimiter *)self;
}

static void leaky_bucket_destroy(pb_ratelimiter *limiter)
{
    pb_ratelimiter_leaky_bucket_state *self = (pb_ratelimiter_leaky_bucket_state *)limiter;
    const pb_allocator *allocator;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    pb_free(allocator, self->level, (size_t)self->max_keys * sizeof(uint32_t));
    pb_free(allocator, self->credit, (size_t)self->max_keys * sizeof(uint32_t));
    pb_free(allocator, self->last, (size_t)self->max_keys * sizeof(uint64_t));
    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self, sizeof(pb_ratelimiter_leaky_bucket_state));
}

static bool leaky_bucket_allow(pb_ratelimiter *limiter, uint64_t key, uint32_t cost,
                               uint64_t now_ms)
{
    pb_ratelimiter_leaky_bucket_state *self = (pb_ratelimiter_leaky_bucket_state *)limiter;
    uint32_t slot;

    assert(self != NULL);

    if (!leaky_bucket_slot_for(self, key, now_ms, &slot)) {
        return false;
    }
    if ((uint64_t)self->level[slot] + (uint64_t)cost > (uint64_t)self->capacity) {
        return false;
    }
    self->level[slot] += cost;
    return true;
}

static uint64_t leaky_bucket_retry_after(pb_ratelimiter *limiter, uint64_t key, uint64_t now_ms)
{
    pb_ratelimiter_leaky_bucket_state *self = (pb_ratelimiter_leaky_bucket_state *)limiter;
    uint32_t slot;
    uint32_t deficit;

    assert(self != NULL);

    if (!pb_map_get(&self->index, key, &slot)) {
        /* Untracked. With room in the table this key would be admitted right
         * now, so zero is the truth; with the table full it will never be
         * admitted, and zero would be a lie a caller acts on. */
        return self->used >= self->max_keys ? PB_RATELIMITER_RETRY_UNKNOWN : 0u;
    }

    drain(self, slot, now_ms);
    if (self->level[slot] < self->capacity) {
        return 0u;
    }

    deficit = 1000u - self->credit[slot];
    return ((uint64_t)deficit + (uint64_t)self->rate_per_sec - 1u) / (uint64_t)self->rate_per_sec;
}

static size_t leaky_bucket_state_size(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_leaky_bucket_state *self =
        (const pb_ratelimiter_leaky_bucket_state *)limiter;
    assert(self != NULL);
    return (size_t)self->used;
}

static size_t leaky_bucket_memory_bytes(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_leaky_bucket_state *self =
        (const pb_ratelimiter_leaky_bucket_state *)limiter;
    assert(self != NULL);
    return sizeof(pb_ratelimiter_leaky_bucket_state) +
           (size_t)self->max_keys * (2u * sizeof(uint32_t) + sizeof(uint64_t)) +
           pb_map_memory_bytes(&self->index);
}

const pb_ratelimiter_vtable pb_ratelimiter_leaky_bucket = { leaky_bucket_create,
                                                            leaky_bucket_allow,
                                                            leaky_bucket_retry_after,
                                                            leaky_bucket_state_size,
                                                            leaky_bucket_memory_bytes,
                                                            leaky_bucket_destroy };

/* ========================================================================
 * src/rate_limiter/sliding_counter.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/sliding-counter/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * SlidingCounter — two fixed windows, weighted by how far into the new one you are.
 *
 * Mirrors index.ts and policy.py. A map from key to slot, and three parallel
 * arrays: which window the slot is in, its count, and the previous window's.
 */




/* A slot that has never been rolled forward. No real window start can equal it,
 * because window starts are multiples of window_ms below any plausible clock. */
#define PB_SLIDING_COUNTER_UNSET UINT64_MAX

typedef struct pb_ratelimiter_sliding_counter_state {
    const pb_allocator *allocator;
    pb_map index; /* key -> slot */
    uint64_t *window_start;
    uint32_t *current;
    uint32_t *previous;
    uint32_t limit;
    uint32_t window_ms;
    uint32_t max_keys;
    uint32_t used;
} pb_ratelimiter_sliding_counter_state;

/* The start of the window containing `now`. Integer division, no floats. */
static uint64_t sliding_counter_window_of(const pb_ratelimiter_sliding_counter_state *self, uint64_t now)
{
    return now - (now % (uint64_t)self->window_ms);
}

/* Roll a slot's counters forward to `start`. */
static void sliding_counter_advance(pb_ratelimiter_sliding_counter_state *self, uint32_t slot, uint64_t start)
{
    uint64_t previous_start = self->window_start[slot];

    if (previous_start == start) {
        return;
    }

    if (previous_start != PB_SLIDING_COUNTER_UNSET &&
        start == previous_start + (uint64_t)self->window_ms) {
        /* The very next window: today's count becomes yesterday's. */
        self->previous[slot] = self->current[slot];
    } else {
        /* A gap of two windows or more — nothing from before is still in the
         * trailing window, so both counts go. */
        self->previous[slot] = 0u;
    }
    self->current[slot] = 0u;
    self->window_start[slot] = start;
}

/*
 * The weighted estimate of requests in the trailing window.
 *
 * `previous * (window_ms - elapsed) / window_ms + current`, with the remainder
 * discarded. The multiply is done in 64 bits so a large limit cannot overflow
 * before the division brings it back down.
 */
static uint64_t estimate(const pb_ratelimiter_sliding_counter_state *self, uint32_t slot,
                         uint64_t now_ms)
{
    uint64_t elapsed = now_ms - self->window_start[slot];
    uint64_t carried = ((uint64_t)self->previous[slot] * ((uint64_t)self->window_ms - elapsed)) /
                       (uint64_t)self->window_ms;
    return carried + (uint64_t)self->current[slot];
}

/*
 * Find a key's slot, claiming a free one if it has never been seen.
 *
 * Returns false when the table is full, which is the fail-closed case: a new
 * key is refused rather than being silently let through.
 */
static bool sliding_counter_slot_for(pb_ratelimiter_sliding_counter_state *self, uint64_t key, uint32_t *slot)
{
    if (pb_map_get(&self->index, key, slot)) {
        return true;
    }
    if (self->used >= self->max_keys) {
        return false;
    }
    *slot = self->used;
    if (!pb_map_put(&self->index, key, *slot)) {
        return false;
    }
    self->used += 1u;
    self->window_start[*slot] = PB_SLIDING_COUNTER_UNSET;
    self->current[*slot] = 0u;
    self->previous[*slot] = 0u;
    return true;
}

static pb_ratelimiter *sliding_counter_create(const void *params, const pb_allocator *allocator,
                                              pb_rng *rng)
{
    const pb_ratelimiter_sliding_counter_params *config =
        (const pb_ratelimiter_sliding_counter_params *)params;
    pb_ratelimiter_sliding_counter_state *self;
    uint32_t limit;
    uint32_t window_ms;
    uint32_t max_keys;

    (void)rng; /* a sliding counter makes no random choices */

    limit = (config == NULL) ? 100u : config->limit;
    window_ms = (config == NULL) ? 1000u : config->window_ms;
    max_keys = (config == NULL) ? 1024u : config->max_keys;
    if (limit == 0u || window_ms == 0u || max_keys == 0u) {
        return NULL;
    }

    self = (pb_ratelimiter_sliding_counter_state *)pb_alloc(
        allocator, sizeof(pb_ratelimiter_sliding_counter_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->limit = limit;
    self->window_ms = window_ms;
    self->max_keys = max_keys;
    self->used = 0u;
    self->window_start = NULL;
    self->current = NULL;
    self->previous = NULL;

    if (!pb_map_init(&self->index, max_keys, allocator)) {
        pb_free(allocator, self, sizeof(pb_ratelimiter_sliding_counter_state));
        return NULL;
    }

    self->window_start = (uint64_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint64_t));
    self->current = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->previous = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    if (self->window_start == NULL || self->current == NULL || self->previous == NULL) {
        pb_free(allocator, self->window_start, (size_t)max_keys * sizeof(uint64_t));
        pb_free(allocator, self->current, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->previous, (size_t)max_keys * sizeof(uint32_t));
        pb_map_destroy(&self->index, allocator);
        pb_free(allocator, self, sizeof(pb_ratelimiter_sliding_counter_state));
        return NULL;
    }

    return (pb_ratelimiter *)self;
}

static void sliding_counter_destroy(pb_ratelimiter *limiter)
{
    pb_ratelimiter_sliding_counter_state *self = (pb_ratelimiter_sliding_counter_state *)limiter;
    const pb_allocator *allocator;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    pb_free(allocator, self->window_start, (size_t)self->max_keys * sizeof(uint64_t));
    pb_free(allocator, self->current, (size_t)self->max_keys * sizeof(uint32_t));
    pb_free(allocator, self->previous, (size_t)self->max_keys * sizeof(uint32_t));
    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self, sizeof(pb_ratelimiter_sliding_counter_state));
}

static bool sliding_counter_allow(pb_ratelimiter *limiter, uint64_t key, uint32_t cost,
                                  uint64_t now_ms)
{
    pb_ratelimiter_sliding_counter_state *self = (pb_ratelimiter_sliding_counter_state *)limiter;
    uint32_t slot;

    assert(self != NULL);

    if (!sliding_counter_slot_for(self, key, &slot)) {
        return false;
    }

    sliding_counter_advance(self, slot, sliding_counter_window_of(self, now_ms));
    if (estimate(self, slot, now_ms) + (uint64_t)cost > (uint64_t)self->limit) {
        return false;
    }
    self->current[slot] += cost;
    return true;
}

static uint64_t sliding_counter_retry_after(pb_ratelimiter *limiter, uint64_t key,
                                            uint64_t now_ms)
{
    pb_ratelimiter_sliding_counter_state *self = (pb_ratelimiter_sliding_counter_state *)limiter;
    uint64_t start;
    uint64_t elapsed;
    uint64_t target;
    uint32_t slot;
    uint32_t current;
    uint32_t previous;
    uint32_t need;
    uint32_t excess;

    assert(self != NULL);

    if (!pb_map_get(&self->index, key, &slot)) {
        /* Untracked. With room in the table this key would be admitted right
         * now, so zero is the truth; with the table full it will never be
         * admitted, and zero would be a lie a caller acts on. */
        return self->used >= self->max_keys ? PB_RATELIMITER_RETRY_UNKNOWN : 0u;
    }

    start = sliding_counter_window_of(self, now_ms);
    sliding_counter_advance(self, slot, start);
    if (estimate(self, slot, now_ms) < (uint64_t)self->limit) {
        return 0u;
    }

    current = self->current[slot];
    previous = self->previous[slot];

    if (current + 1u > self->limit) {
        /* The current window has reached the limit on its own, so the wait runs
         * past the edge — but not only to the edge. At the edge this count
         * becomes the previous count and, undecayed, still refuses. `allow`
         * never lets `current` exceed `limit`, so one further millisecond of
         * decay is always enough. */
        return start + (uint64_t)self->window_ms - now_ms + 1u;
    }
    if (previous == 0u) {
        return 0u;
    }

    /* Admission needs the carried part to fall to `limit - current - 1` or
     * below. It decays linearly, so the smallest qualifying elapsed follows
     * directly:
     *
     *   carried <= need  <=>  previous * (window_ms - elapsed) < (need + 1) * window_ms
     *                    <=>  elapsed > window_ms * (previous - need - 1) / previous
     *
     * An excess of zero means the carried count is exactly one too high, which
     * still needs a millisecond of decay — so only a smaller previous admits
     * immediately. */
    need = self->limit - current - 1u;
    if (previous < need + 1u) {
        return 0u;
    }
    excess = previous - need - 1u;

    target = ((uint64_t)self->window_ms * (uint64_t)excess) / (uint64_t)previous + 1u;
    elapsed = now_ms - start;
    return target > elapsed ? target - elapsed : 0u;
}

static size_t sliding_counter_state_size(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_sliding_counter_state *self =
        (const pb_ratelimiter_sliding_counter_state *)limiter;
    assert(self != NULL);
    return (size_t)self->used;
}

static size_t sliding_counter_memory_bytes(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_sliding_counter_state *self =
        (const pb_ratelimiter_sliding_counter_state *)limiter;
    assert(self != NULL);
    return sizeof(pb_ratelimiter_sliding_counter_state) +
           (size_t)self->max_keys * (sizeof(uint64_t) + 2u * sizeof(uint32_t)) +
           pb_map_memory_bytes(&self->index);
}

const pb_ratelimiter_vtable pb_ratelimiter_sliding_counter = { sliding_counter_create,
                                                               sliding_counter_allow,
                                                               sliding_counter_retry_after,
                                                               sliding_counter_state_size,
                                                               sliding_counter_memory_bytes,
                                                               sliding_counter_destroy };

/* ========================================================================
 * src/rate_limiter/sliding_log.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/sliding-log/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * SlidingLog — remember every request time, and count the recent ones.
 *
 * Mirrors index.ts and policy.py. A map from key to slot, and one flat block of
 * `max_keys * limit` timestamps carved into a ring per slot — one allocation
 * rather than one per key, so a key's first request costs nothing extra.
 */




typedef struct pb_ratelimiter_sliding_log_state {
    const pb_allocator *allocator;
    pb_map index;    /* key -> slot */
    uint64_t *times; /* max_keys rings of `limit` timestamps, laid end to end */
    uint32_t *head;  /* oldest live entry in each ring */
    uint32_t *count; /* live entries in each ring */
    uint32_t limit;
    uint32_t window_ms;
    uint32_t max_keys;
    uint32_t used;
} pb_ratelimiter_sliding_log_state;

/* The base of a slot's ring inside the flat block. */
static uint64_t *ring_of(pb_ratelimiter_sliding_log_state *self, uint32_t slot)
{
    return self->times + (size_t)slot * (size_t)self->limit;
}

/*
 * Drop every timestamp that has left the window.
 *
 * The window is (now - window_ms, now]: an entry exactly window_ms old has
 * left it. That is what makes "at most `limit` in any window_ms interval" true
 * as stated rather than off by one at the edge.
 *
 * O(1) amortised: each timestamp is written once and dropped once, so this loop
 * does constant work per admitted request however bursty the arrivals are.
 */
static void expire(pb_ratelimiter_sliding_log_state *self, uint32_t slot, uint64_t now_ms)
{
    const uint64_t *ring = ring_of(self, slot);

    /* Unsigned arithmetic: before window_ms has elapsed nothing can have aged
     * out, and computing `now - window_ms` would wrap. */
    if (now_ms < (uint64_t)self->window_ms) {
        return;
    }

    while (self->count[slot] > 0u && ring[self->head[slot]] <= now_ms - (uint64_t)self->window_ms) {
        self->head[slot] = (self->head[slot] + 1u) % self->limit;
        self->count[slot] -= 1u;
    }
}

/*
 * Find a key's slot, claiming a free one if it has never been seen.
 *
 * Returns false when the table is full, which is the fail-closed case: a new
 * key is refused rather than being silently let through.
 */
static bool sliding_log_slot_for(pb_ratelimiter_sliding_log_state *self, uint64_t key, uint32_t *slot)
{
    if (pb_map_get(&self->index, key, slot)) {
        return true;
    }
    if (self->used >= self->max_keys) {
        return false;
    }
    *slot = self->used;
    if (!pb_map_put(&self->index, key, *slot)) {
        return false;
    }
    self->used += 1u;
    self->head[*slot] = 0u;
    self->count[*slot] = 0u;
    return true;
}

static pb_ratelimiter *sliding_log_create(const void *params, const pb_allocator *allocator,
                                          pb_rng *rng)
{
    const pb_ratelimiter_sliding_log_params *config =
        (const pb_ratelimiter_sliding_log_params *)params;
    pb_ratelimiter_sliding_log_state *self;
    uint32_t limit;
    uint32_t window_ms;
    uint32_t max_keys;
    size_t entries;

    (void)rng; /* a sliding log makes no random choices */

    limit = (config == NULL) ? 100u : config->limit;
    window_ms = (config == NULL) ? 1000u : config->window_ms;
    max_keys = (config == NULL) ? 1024u : config->max_keys;
    if (limit == 0u || window_ms == 0u || max_keys == 0u) {
        return NULL;
    }

    self = (pb_ratelimiter_sliding_log_state *)pb_alloc(
        allocator, sizeof(pb_ratelimiter_sliding_log_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->limit = limit;
    self->window_ms = window_ms;
    self->max_keys = max_keys;
    self->used = 0u;
    self->times = NULL;
    self->head = NULL;
    self->count = NULL;

    if (!pb_map_init(&self->index, max_keys, allocator)) {
        pb_free(allocator, self, sizeof(pb_ratelimiter_sliding_log_state));
        return NULL;
    }

    entries = (size_t)max_keys * (size_t)limit;
    self->times = (uint64_t *)pb_alloc(allocator, entries * sizeof(uint64_t));
    self->head = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->count = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    if (self->times == NULL || self->head == NULL || self->count == NULL) {
        pb_free(allocator, self->times, entries * sizeof(uint64_t));
        pb_free(allocator, self->head, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->count, (size_t)max_keys * sizeof(uint32_t));
        pb_map_destroy(&self->index, allocator);
        pb_free(allocator, self, sizeof(pb_ratelimiter_sliding_log_state));
        return NULL;
    }

    return (pb_ratelimiter *)self;
}

static void sliding_log_destroy(pb_ratelimiter *limiter)
{
    pb_ratelimiter_sliding_log_state *self = (pb_ratelimiter_sliding_log_state *)limiter;
    const pb_allocator *allocator;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    pb_free(allocator, self->times,
            (size_t)self->max_keys * (size_t)self->limit * sizeof(uint64_t));
    pb_free(allocator, self->head, (size_t)self->max_keys * sizeof(uint32_t));
    pb_free(allocator, self->count, (size_t)self->max_keys * sizeof(uint32_t));
    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self, sizeof(pb_ratelimiter_sliding_log_state));
}

static bool sliding_log_allow(pb_ratelimiter *limiter, uint64_t key, uint32_t cost,
                              uint64_t now_ms)
{
    pb_ratelimiter_sliding_log_state *self = (pb_ratelimiter_sliding_log_state *)limiter;
    uint64_t *ring;
    uint32_t slot;
    uint32_t unit;

    assert(self != NULL);

    if (!sliding_log_slot_for(self, key, &slot)) {
        return false;
    }

    expire(self, slot, now_ms);
    if ((uint64_t)self->count[slot] + (uint64_t)cost > (uint64_t)self->limit) {
        return false;
    }

    /* A request costing n occupies n slots, all stamped with the same instant,
     * so they age out together. */
    ring = ring_of(self, slot);
    for (unit = 0u; unit < cost; ++unit) {
        ring[(self->head[slot] + self->count[slot]) % self->limit] = now_ms;
        self->count[slot] += 1u;
    }
    return true;
}

static uint64_t sliding_log_retry_after(pb_ratelimiter *limiter, uint64_t key, uint64_t now_ms)
{
    pb_ratelimiter_sliding_log_state *self = (pb_ratelimiter_sliding_log_state *)limiter;
    const uint64_t *ring;
    uint32_t slot;

    assert(self != NULL);

    if (!pb_map_get(&self->index, key, &slot)) {
        /* Untracked. With room in the table this key would be admitted right
         * now, so zero is the truth; with the table full it will never be
         * admitted, and zero would be a lie a caller acts on. */
        return self->used >= self->max_keys ? PB_RATELIMITER_RETRY_UNKNOWN : 0u;
    }

    expire(self, slot, now_ms);
    if (self->count[slot] < self->limit) {
        return 0u;
    }

    /* The oldest entry leaves the window at `time + window_ms`, because the
     * window excludes its far end — so that instant is when room appears, not
     * the millisecond after it. */
    ring = ring_of(self, slot);
    return ring[self->head[slot]] + (uint64_t)self->window_ms - now_ms;
}

static size_t sliding_log_state_size(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_sliding_log_state *self =
        (const pb_ratelimiter_sliding_log_state *)limiter;
    assert(self != NULL);
    return (size_t)self->used;
}

static size_t sliding_log_memory_bytes(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_sliding_log_state *self =
        (const pb_ratelimiter_sliding_log_state *)limiter;
    assert(self != NULL);
    return sizeof(pb_ratelimiter_sliding_log_state) +
           (size_t)self->max_keys * (size_t)self->limit * sizeof(uint64_t) +
           (size_t)self->max_keys * 2u * sizeof(uint32_t) +
           pb_map_memory_bytes(&self->index);
}

const pb_ratelimiter_vtable pb_ratelimiter_sliding_log = { sliding_log_create,
                                                           sliding_log_allow,
                                                           sliding_log_retry_after,
                                                           sliding_log_state_size,
                                                           sliding_log_memory_bytes,
                                                           sliding_log_destroy };

/* ========================================================================
 * src/rate_limiter/token_bucket.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/token-bucket/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * TokenBucket — spend from a balance that refills at a steady rate.
 *
 * Mirrors index.ts and policy.py. A map from key to slot, and three parallel
 * arrays: the whole tokens, the thousandths carried between refills, and when
 * the slot was last touched.
 */




typedef struct pb_ratelimiter_token_bucket_state {
    const pb_allocator *allocator;
    pb_map index;     /* key -> slot */
    uint32_t *tokens; /* whole tokens available */
    uint32_t *credit; /* thousandths of a token, always 0..999 */
    uint64_t *last;   /* when the slot was last brought up to date */
    uint32_t rate_per_sec;
    uint32_t burst;
    uint32_t fill_ms; /* how long an empty bucket takes to fill */
    uint32_t max_keys;
    uint32_t used;
} pb_ratelimiter_token_bucket_state;

/*
 * Bring a bucket up to date at `now_ms`.
 *
 * Idle time is clamped to `fill_ms` before the multiply. Without it a key
 * untouched for a month would compute `rate_per_sec * elapsed` far outside
 * 32 bits; the result is identical either way, because the bucket saturates
 * long before.
 */
static void token_bucket_refill(pb_ratelimiter_token_bucket_state *self, uint32_t slot, uint64_t now_ms)
{
    uint64_t elapsed;
    uint64_t credit;
    uint64_t tokens;

    if (now_ms <= self->last[slot]) {
        return;
    }
    elapsed = now_ms - self->last[slot];
    if (elapsed > (uint64_t)self->fill_ms) {
        elapsed = (uint64_t)self->fill_ms;
    }

    credit = (uint64_t)self->credit[slot] + (uint64_t)self->rate_per_sec * elapsed;
    tokens = (uint64_t)self->tokens[slot] + credit / 1000u;
    credit %= 1000u;

    if (tokens >= (uint64_t)self->burst) {
        /* The bucket overflows: tokens above `burst` are lost, and so is the
         * fraction that would have become the next one. */
        tokens = (uint64_t)self->burst;
        credit = 0u;
    }

    self->tokens[slot] = (uint32_t)tokens;
    self->credit[slot] = (uint32_t)credit;
    self->last[slot] = now_ms;
}

/*
 * Find a key's slot, claiming a free one if it has never been seen.
 *
 * Returns false when the table is full, which is the fail-closed case: a new
 * key is refused rather than being silently let through.
 */
static bool token_bucket_slot_for(pb_ratelimiter_token_bucket_state *self, uint64_t key, uint64_t now_ms,
                     uint32_t *slot)
{
    if (pb_map_get(&self->index, key, slot)) {
        token_bucket_refill(self, *slot, now_ms);
        return true;
    }
    if (self->used >= self->max_keys) {
        return false;
    }
    *slot = self->used;
    if (!pb_map_put(&self->index, key, *slot)) {
        return false;
    }
    self->used += 1u;
    /* A key never seen starts full: it has been idle for all of history. */
    self->tokens[*slot] = self->burst;
    self->credit[*slot] = 0u;
    self->last[*slot] = now_ms;
    return true;
}

static pb_ratelimiter *token_bucket_create(const void *params, const pb_allocator *allocator,
                                           pb_rng *rng)
{
    const pb_ratelimiter_token_bucket_params *config =
        (const pb_ratelimiter_token_bucket_params *)params;
    pb_ratelimiter_token_bucket_state *self;
    uint32_t rate_per_sec;
    uint32_t burst;
    uint32_t max_keys;

    (void)rng; /* a token bucket makes no random choices */

    rate_per_sec = (config == NULL) ? 100u : config->rate_per_sec;
    burst = (config == NULL) ? 100u : config->burst;
    max_keys = (config == NULL) ? 1024u : config->max_keys;
    if (rate_per_sec == 0u || burst == 0u || max_keys == 0u) {
        return NULL;
    }

    self = (pb_ratelimiter_token_bucket_state *)pb_alloc(
        allocator, sizeof(pb_ratelimiter_token_bucket_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->rate_per_sec = rate_per_sec;
    self->burst = burst;
    /* Ceiling division, in 64 bits so a large burst cannot overflow. */
    self->fill_ms =
        (uint32_t)(((uint64_t)burst * 1000u + (uint64_t)rate_per_sec - 1u) / rate_per_sec);
    self->max_keys = max_keys;
    self->used = 0u;
    self->tokens = NULL;
    self->credit = NULL;
    self->last = NULL;

    if (!pb_map_init(&self->index, max_keys, allocator)) {
        pb_free(allocator, self, sizeof(pb_ratelimiter_token_bucket_state));
        return NULL;
    }

    self->tokens = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->credit = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->last = (uint64_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint64_t));
    if (self->tokens == NULL || self->credit == NULL || self->last == NULL) {
        pb_free(allocator, self->tokens, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->credit, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->last, (size_t)max_keys * sizeof(uint64_t));
        pb_map_destroy(&self->index, allocator);
        pb_free(allocator, self, sizeof(pb_ratelimiter_token_bucket_state));
        return NULL;
    }

    return (pb_ratelimiter *)self;
}

static void token_bucket_destroy(pb_ratelimiter *limiter)
{
    pb_ratelimiter_token_bucket_state *self = (pb_ratelimiter_token_bucket_state *)limiter;
    const pb_allocator *allocator;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    pb_free(allocator, self->tokens, (size_t)self->max_keys * sizeof(uint32_t));
    pb_free(allocator, self->credit, (size_t)self->max_keys * sizeof(uint32_t));
    pb_free(allocator, self->last, (size_t)self->max_keys * sizeof(uint64_t));
    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self, sizeof(pb_ratelimiter_token_bucket_state));
}

static bool token_bucket_allow(pb_ratelimiter *limiter, uint64_t key, uint32_t cost,
                               uint64_t now_ms)
{
    pb_ratelimiter_token_bucket_state *self = (pb_ratelimiter_token_bucket_state *)limiter;
    uint32_t slot;

    assert(self != NULL);

    if (!token_bucket_slot_for(self, key, now_ms, &slot)) {
        return false;
    }
    if (self->tokens[slot] < cost) {
        return false;
    }
    self->tokens[slot] -= cost;
    return true;
}

static uint64_t token_bucket_retry_after(pb_ratelimiter *limiter, uint64_t key, uint64_t now_ms)
{
    pb_ratelimiter_token_bucket_state *self = (pb_ratelimiter_token_bucket_state *)limiter;
    uint32_t slot;
    uint32_t deficit;

    assert(self != NULL);

    if (!pb_map_get(&self->index, key, &slot)) {
        /* Untracked. With room in the table this key would be admitted right
         * now, so zero is the truth; with the table full it will never be
         * admitted, and zero would be a lie a caller acts on. */
        return self->used >= self->max_keys ? PB_RATELIMITER_RETRY_UNKNOWN : 0u;
    }

    token_bucket_refill(self, slot, now_ms);
    if (self->tokens[slot] >= 1u) {
        return 0u;
    }

    /* Ceiling division: the token arrives at the first whole millisecond where
     * the credit reaches 1,000. */
    deficit = 1000u - self->credit[slot];
    return ((uint64_t)deficit + (uint64_t)self->rate_per_sec - 1u) / (uint64_t)self->rate_per_sec;
}

static size_t token_bucket_state_size(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_token_bucket_state *self =
        (const pb_ratelimiter_token_bucket_state *)limiter;
    assert(self != NULL);
    return (size_t)self->used;
}

static size_t token_bucket_memory_bytes(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_token_bucket_state *self =
        (const pb_ratelimiter_token_bucket_state *)limiter;
    assert(self != NULL);
    return sizeof(pb_ratelimiter_token_bucket_state) +
           (size_t)self->max_keys * (2u * sizeof(uint32_t) + sizeof(uint64_t)) +
           pb_map_memory_bytes(&self->index);
}

const pb_ratelimiter_vtable pb_ratelimiter_token_bucket = { token_bucket_create,
                                                            token_bucket_allow,
                                                            token_bucket_retry_after,
                                                            token_bucket_state_size,
                                                            token_bucket_memory_bytes,
                                                            token_bucket_destroy };

/* ========================================================================
 * src/rate_limiter/traces.c
 * ======================================================================== */

/* The `bursty` cycle: this long in total, of which the first part is ON. */
#define PB_BURST_CYCLE_MS 2000u
#define PB_BURST_ON_MS 200u

const pb_ratelimiter_trace_spec pb_ratelimiter_traces[] = {
    { "steady", PB_RATELIMITER_TRACE_SINGLE_KEY, 60000u, 1u, 0u, 0.09, 50u },
    { "bursty", PB_RATELIMITER_TRACE_BURSTY, 60000u, 1u, 0u, 0.5, 51u },
    { "many-keys", PB_RATELIMITER_TRACE_MANY_KEYS, 120000u, 10000u, 10000u, 0.5, 52u },
    /* Three times the reference limit, sustained. The same generator as
     * `steady` at a rate no limiter can meet. */
    { "overload", PB_RATELIMITER_TRACE_SINGLE_KEY, 60000u, 1u, 0u, 0.3, 53u }
};

const pb_ratelimiter_trace_spec *pb_ratelimiter_trace_find(const char *id)
{
    size_t i;

    if (id == NULL) {
        return NULL;
    }
    for (i = 0; i < (size_t)PB_RATELIMITER_TRACE_COUNT; ++i) {
        if (strcmp(pb_ratelimiter_traces[i].id, id) == 0) {
            return &pb_ratelimiter_traces[i];
        }
    }
    return NULL;
}

/*
 * A steady stream on a single key: one Bernoulli draw per millisecond.
 *
 * Shared by `steady` and `overload`, which differ only in the arrival rate —
 * one just under the reference limit and one three times over it.
 */
static size_t generate_single_key(const pb_ratelimiter_trace_spec *spec, uint32_t *times,
                                  uint32_t *keys, size_t limit)
{
    pb_rng rng;
    size_t written = 0;
    uint32_t t;

    pb_rng_init(&rng, spec->seed);

    for (t = 0; t < spec->duration_ms && written < limit; ++t) {
        if (pb_rng_next_float(&rng) < spec->arrival_p) {
            times[written] = t;
            keys[written] = 0u;
            written += 1;
        }
    }
    return written;
}

/*
 * Silence, then a burst, repeating.
 *
 * A millisecond in the OFF phase consumes no random draw. That is the pinned
 * call order and every port matches it: drawing during the silence would be
 * equally valid as a definition and would produce a completely different trace
 * from the same seed.
 */
static size_t generate_bursty(const pb_ratelimiter_trace_spec *spec, uint32_t *times,
                              uint32_t *keys, size_t limit)
{
    pb_rng rng;
    size_t written = 0;
    uint32_t t;

    pb_rng_init(&rng, spec->seed);

    for (t = 0; t < spec->duration_ms && written < limit; ++t) {
        if (t % PB_BURST_CYCLE_MS >= PB_BURST_ON_MS) {
            continue;
        }
        if (pb_rng_next_float(&rng) < spec->arrival_p) {
            times[written] = t;
            keys[written] = 0u;
            written += 1;
        }
    }
    return written;
}

/*
 * A busy stream spread over a skewed keyspace.
 *
 * The draw order is pinned: the Bernoulli draw comes first, and the Zipf sample
 * is taken only when that draw produced an arrival. Sampling a key
 * unconditionally would consume the stream at a different rate and diverge.
 */
static size_t generate_many_keys(const pb_ratelimiter_trace_spec *spec, uint32_t *times,
                                 uint32_t *keys, size_t limit,
                                 const pb_allocator *allocator)
{
    pb_rng rng;
    pb_zipf zipf;
    size_t written = 0;
    uint32_t t;

    pb_rng_init(&rng, spec->seed);
    if (!pb_zipf_init(&zipf, spec->keyspace, 1.0, allocator)) {
        return 0;
    }

    for (t = 0; t < spec->duration_ms && written < limit; ++t) {
        if (pb_rng_next_float(&rng) < spec->arrival_p) {
            times[written] = t;
            keys[written] = pb_zipf_sample(&zipf, &rng);
            written += 1;
        }
    }

    pb_zipf_destroy(&zipf, allocator);
    return written;
}

size_t pb_ratelimiter_trace_generate(const pb_ratelimiter_trace_spec *spec, uint32_t *times,
                                     uint32_t *keys, size_t max_events,
                                     const pb_allocator *allocator)
{
    if (spec == NULL || times == NULL || keys == NULL || max_events == 0) {
        return 0;
    }

    switch (spec->kind) {
    case PB_RATELIMITER_TRACE_SINGLE_KEY:
        return generate_single_key(spec, times, keys, max_events);
    case PB_RATELIMITER_TRACE_BURSTY:
        return generate_bursty(spec, times, keys, max_events);
    case PB_RATELIMITER_TRACE_MANY_KEYS:
        return generate_many_keys(spec, times, keys, max_events, allocator);
    default:
        return 0;
    }
}

/* ========================================================================
 * src/retry/constant.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/retry/constant/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * Constant — wait the same amount every time.
 *
 * Mirrors index.ts and policy.py. Two integers and no state at all: asking
 * twice gives the same answer, which is exactly what synchronises a fleet.
 */




typedef struct pb_retry_constant_state {
    const pb_allocator *allocator;
    uint32_t base_ms;
    uint32_t max_attempts;
} pb_retry_constant_state;

static pb_retry *constant_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_retry_constant_params *config = (const pb_retry_constant_params *)params;
    pb_retry_constant_state *self;
    uint32_t base_ms;
    uint32_t max_attempts;

    (void)rng; /* draws nothing, which is the whole problem with it */

    base_ms = (config == NULL) ? 100u : config->base_ms;
    max_attempts = (config == NULL) ? 8u : config->max_attempts;
    /* base_ms of zero is legitimate — retrying immediately is aggressive, not
     * invalid — so only the attempt budget has a floor. */
    if (max_attempts == 0u) {
        return NULL;
    }

    self = (pb_retry_constant_state *)pb_alloc(allocator, sizeof(pb_retry_constant_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->base_ms = base_ms;
    self->max_attempts = max_attempts;
    return (pb_retry *)self;
}

static void constant_destroy(pb_retry *policy)
{
    pb_retry_constant_state *self = (pb_retry_constant_state *)policy;

    if (self == NULL) {
        return;
    }
    pb_free(self->allocator, self, sizeof(pb_retry_constant_state));
}

static int64_t constant_next_delay(pb_retry *policy, uint32_t attempt,
                                   const pb_retry_error *error)
{
    pb_retry_constant_state *self = (pb_retry_constant_state *)policy;

    assert(self != NULL);
    assert(error != NULL);

    /* Nothing is gained by retrying a failure the server says is permanent. */
    if (!error->retryable) {
        return PB_RETRY_GIVE_UP;
    }
    if (attempt >= self->max_attempts) {
        return PB_RETRY_GIVE_UP;
    }
    return (int64_t)self->base_ms;
}

static size_t constant_memory_bytes(const pb_retry *policy)
{
    (void)policy;
    return sizeof(pb_retry_constant_state);
}

const pb_retry_vtable pb_retry_constant = { constant_create, constant_next_delay,
                                            constant_memory_bytes, constant_destroy };

/* ========================================================================
 * src/retry/decorrelated_jitter.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/retry/decorrelated-jitter/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * DecorrelatedJitter — grow the delay from the last delay, not the attempt number.
 *
 * Mirrors index.ts and policy.py. The only stateful policy in the domain: the
 * walk lives in `previous_ms`, and the object's lifetime is the retry sequence.
 */




typedef struct pb_retry_decorrelated_jitter_state {
    const pb_allocator *allocator;
    pb_rng *rng;  /* borrowed from the caller */
    pb_rng owned; /* used only when the caller supplied none */
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;
    uint32_t previous_ms; /* the walk; starts at base_ms */
} pb_retry_decorrelated_jitter_state;

static pb_retry *decorrelated_create(const void *params, const pb_allocator *allocator,
                                     pb_rng *rng)
{
    const pb_retry_decorrelated_jitter_params *config =
        (const pb_retry_decorrelated_jitter_params *)params;
    pb_retry_decorrelated_jitter_state *self;
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;

    base_ms = (config == NULL) ? 100u : config->base_ms;
    cap_ms = (config == NULL) ? 10000u : config->cap_ms;
    max_attempts = (config == NULL) ? 8u : config->max_attempts;
    if (base_ms == 0u || cap_ms == 0u || max_attempts == 0u) {
        return NULL;
    }
    /* `prev * 3` must stay inside 32 bits. `prev` never exceeds `cap_ms`, so
     * bounding the cap bounds the whole walk. */
    if (cap_ms > UINT32_MAX / 3u) {
        return NULL;
    }

    self = (pb_retry_decorrelated_jitter_state *)pb_alloc(
        allocator, sizeof(pb_retry_decorrelated_jitter_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->base_ms = base_ms;
    self->cap_ms = cap_ms;
    self->max_attempts = max_attempts;
    self->previous_ms = base_ms;

    if (rng != NULL) {
        self->rng = rng;
    } else {
        pb_rng_init(&self->owned, 0u);
        self->rng = &self->owned;
    }
    return (pb_retry *)self;
}

static void decorrelated_destroy(pb_retry *policy)
{
    pb_retry_decorrelated_jitter_state *self = (pb_retry_decorrelated_jitter_state *)policy;

    if (self == NULL) {
        return;
    }
    pb_free(self->allocator, self, sizeof(pb_retry_decorrelated_jitter_state));
}

static int64_t decorrelated_next_delay(pb_retry *policy, uint32_t attempt,
                                       const pb_retry_error *error)
{
    pb_retry_decorrelated_jitter_state *self = (pb_retry_decorrelated_jitter_state *)policy;
    uint32_t span;
    uint32_t delay;

    assert(self != NULL);
    assert(error != NULL);

    /* Both refusals precede the draw, so a declined call leaves neither the
     * stream nor the walk advanced. */
    if (!error->retryable) {
        return PB_RETRY_GIVE_UP;
    }
    if (attempt >= self->max_attempts) {
        return PB_RETRY_GIVE_UP;
    }

    /* `prev * 3 - base` is always positive: `prev` starts at `base` and every
     * delay is at least `base`, so the smallest span is `2 * base`. */
    span = self->previous_ms * 3u - self->base_ms;
    delay = self->base_ms + pb_rng_next_int(self->rng, span + 1u);
    if (delay > self->cap_ms) {
        delay = self->cap_ms;
    }

    /* The walk advances from the delay actually used, cap included — otherwise
     * a client at the cap would keep drawing from an ever-growing range it can
     * never reach. */
    self->previous_ms = delay;
    return (int64_t)delay;
}

static size_t decorrelated_memory_bytes(const pb_retry *policy)
{
    (void)policy;
    return sizeof(pb_retry_decorrelated_jitter_state);
}

const pb_retry_vtable pb_retry_decorrelated_jitter = { decorrelated_create,
                                                       decorrelated_next_delay,
                                                       decorrelated_memory_bytes,
                                                       decorrelated_destroy };

/* ========================================================================
 * src/retry/equal_jitter.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/retry/equal-jitter/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * EqualJitter — half the exponential delay fixed, half of it random.
 *
 * Mirrors index.ts and policy.py. The ceiling is restated here rather than
 * shared, for the same reason the other ports restate it: a policy file is
 * copied out of the registry whole, and a copy that reached into a sibling
 * policy would not build in the reader's project.
 */




typedef struct pb_retry_equal_jitter_state {
    const pb_allocator *allocator;
    pb_rng *rng;  /* borrowed from the caller */
    pb_rng owned; /* used only when the caller supplied none */
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;
} pb_retry_equal_jitter_state;

/* min(cap, base * 2^(attempt - 1)), in integers and without overflowing. */
static uint32_t equal_jitter_backoff_ceiling(uint32_t attempt, uint32_t base_ms, uint32_t cap_ms)
{
    /* 64-bit, as the float64 reference effectively is: with a cap above 2^31
     * the doubling that passes it would wrap a uint32 and dodge the clamp. */
    uint64_t delay = base_ms;
    uint32_t step;

    for (step = 1u; step < attempt; ++step) {
        if (delay >= cap_ms) {
            return cap_ms;
        }
        delay *= 2u;
    }
    return delay < cap_ms ? (uint32_t)delay : cap_ms;
}

static pb_retry *equal_jitter_create(const void *params, const pb_allocator *allocator,
                                     pb_rng *rng)
{
    const pb_retry_equal_jitter_params *config =
        (const pb_retry_equal_jitter_params *)params;
    pb_retry_equal_jitter_state *self;
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;

    base_ms = (config == NULL) ? 100u : config->base_ms;
    cap_ms = (config == NULL) ? 10000u : config->cap_ms;
    max_attempts = (config == NULL) ? 8u : config->max_attempts;
    if (base_ms == 0u || cap_ms == 0u || max_attempts == 0u) {
        return NULL;
    }

    self = (pb_retry_equal_jitter_state *)pb_alloc(allocator,
                                                   sizeof(pb_retry_equal_jitter_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->base_ms = base_ms;
    self->cap_ms = cap_ms;
    self->max_attempts = max_attempts;

    if (rng != NULL) {
        self->rng = rng;
    } else {
        pb_rng_init(&self->owned, 0u);
        self->rng = &self->owned;
    }
    return (pb_retry *)self;
}

static void equal_jitter_destroy(pb_retry *policy)
{
    pb_retry_equal_jitter_state *self = (pb_retry_equal_jitter_state *)policy;

    if (self == NULL) {
        return;
    }
    pb_free(self->allocator, self, sizeof(pb_retry_equal_jitter_state));
}

static int64_t equal_jitter_next_delay(pb_retry *policy, uint32_t attempt,
                                       const pb_retry_error *error)
{
    pb_retry_equal_jitter_state *self = (pb_retry_equal_jitter_state *)policy;
    uint32_t half;

    assert(self != NULL);
    assert(error != NULL);

    /* Both refusals precede the draw, so a declined call leaves the stream
     * exactly where it was. */
    if (!error->retryable) {
        return PB_RETRY_GIVE_UP;
    }
    if (attempt >= self->max_attempts) {
        return PB_RETRY_GIVE_UP;
    }

    /* Integer halving. At a ceiling of 1 the half is 0 and every delay is 0. */
    half = equal_jitter_backoff_ceiling(attempt, self->base_ms, self->cap_ms) / 2u;
    return (int64_t)half + (int64_t)pb_rng_next_int(self->rng, half + 1u);
}

static size_t equal_jitter_memory_bytes(const pb_retry *policy)
{
    (void)policy;
    return sizeof(pb_retry_equal_jitter_state);
}

const pb_retry_vtable pb_retry_equal_jitter = { equal_jitter_create, equal_jitter_next_delay,
                                                equal_jitter_memory_bytes,
                                                equal_jitter_destroy };

/* ========================================================================
 * src/retry/exponential.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/retry/exponential/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * Exponential — double the wait after every failure, up to a cap.
 *
 * Mirrors index.ts and policy.py, including the shape of the ceiling
 * computation: it multiplies and stops at the cap rather than shifting, so the
 * arithmetic cannot run past the width of the integer however large `attempt`
 * is. `base << (attempt - 1)` would be undefined behaviour at attempt 33.
 */




typedef struct pb_retry_exponential_state {
    const pb_allocator *allocator;
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;
} pb_retry_exponential_state;

/*
 * min(cap, base * 2^(attempt - 1)), in integers and without overflowing.
 *
 * The same function the jittered policy restates. Each language keeps its own
 * copy so that a policy file copied out of the registry compiles on its own;
 * the copies are pinned against each other by the shared vectors.
 */
static uint32_t exponential_backoff_ceiling(uint32_t attempt, uint32_t base_ms, uint32_t cap_ms)
{
    /* 64-bit, as the float64 reference effectively is: with a cap above 2^31
     * the doubling that passes it would wrap a uint32 and dodge the clamp. */
    uint64_t delay = base_ms;
    uint32_t step;

    for (step = 1u; step < attempt; ++step) {
        if (delay >= cap_ms) {
            return cap_ms;
        }
        delay *= 2u;
    }
    return delay < cap_ms ? (uint32_t)delay : cap_ms;
}

static pb_retry *exponential_create(const void *params, const pb_allocator *allocator,
                                    pb_rng *rng)
{
    const pb_retry_exponential_params *config = (const pb_retry_exponential_params *)params;
    pb_retry_exponential_state *self;
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;

    (void)rng; /* no draw anywhere, which is why it synchronises */

    base_ms = (config == NULL) ? 100u : config->base_ms;
    cap_ms = (config == NULL) ? 10000u : config->cap_ms;
    max_attempts = (config == NULL) ? 8u : config->max_attempts;
    if (base_ms == 0u || cap_ms == 0u || max_attempts == 0u) {
        return NULL;
    }

    self = (pb_retry_exponential_state *)pb_alloc(allocator,
                                                  sizeof(pb_retry_exponential_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->base_ms = base_ms;
    self->cap_ms = cap_ms;
    self->max_attempts = max_attempts;
    return (pb_retry *)self;
}

static void exponential_destroy(pb_retry *policy)
{
    pb_retry_exponential_state *self = (pb_retry_exponential_state *)policy;

    if (self == NULL) {
        return;
    }
    pb_free(self->allocator, self, sizeof(pb_retry_exponential_state));
}

static int64_t exponential_next_delay(pb_retry *policy, uint32_t attempt,
                                      const pb_retry_error *error)
{
    pb_retry_exponential_state *self = (pb_retry_exponential_state *)policy;

    assert(self != NULL);
    assert(error != NULL);

    if (!error->retryable) {
        return PB_RETRY_GIVE_UP;
    }
    if (attempt >= self->max_attempts) {
        return PB_RETRY_GIVE_UP;
    }
    return (int64_t)exponential_backoff_ceiling(attempt, self->base_ms, self->cap_ms);
}

static size_t exponential_memory_bytes(const pb_retry *policy)
{
    (void)policy;
    return sizeof(pb_retry_exponential_state);
}

const pb_retry_vtable pb_retry_exponential = { exponential_create, exponential_next_delay,
                                               exponential_memory_bytes, exponential_destroy };

/* ========================================================================
 * src/retry/exponential_full_jitter.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/retry/exponential-full-jitter/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * ExponentialFullJitter — a uniform delay between zero and the exponential ceiling.
 *
 * Mirrors index.ts and policy.py. The ceiling is computed exactly as
 * `pb_retry_exponential` computes it, and restated here rather than shared for
 * the same reason the TypeScript and Python ports restate it: a policy file is
 * copied out of the registry whole, and a copy that reached into a sibling
 * policy would not build in the reader's project. The shared vectors pin the
 * two against each other.
 */




typedef struct pb_retry_exponential_full_jitter_state {
    const pb_allocator *allocator;
    pb_rng *rng;   /* borrowed from the caller */
    pb_rng owned;  /* used only when the caller supplied none */
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;
} pb_retry_exponential_full_jitter_state;

/* min(cap, base * 2^(attempt - 1)), in integers and without overflowing. */
static uint32_t exponential_full_jitter_backoff_ceiling(uint32_t attempt, uint32_t base_ms, uint32_t cap_ms)
{
    /* 64-bit, as the float64 reference effectively is: with a cap above 2^31
     * the doubling that passes it would wrap a uint32 and dodge the clamp. */
    uint64_t delay = base_ms;
    uint32_t step;

    for (step = 1u; step < attempt; ++step) {
        if (delay >= cap_ms) {
            return cap_ms;
        }
        delay *= 2u;
    }
    return delay < cap_ms ? (uint32_t)delay : cap_ms;
}

static pb_retry *full_jitter_create(const void *params, const pb_allocator *allocator,
                                    pb_rng *rng)
{
    const pb_retry_exponential_full_jitter_params *config =
        (const pb_retry_exponential_full_jitter_params *)params;
    pb_retry_exponential_full_jitter_state *self;
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;

    base_ms = (config == NULL) ? 100u : config->base_ms;
    cap_ms = (config == NULL) ? 10000u : config->cap_ms;
    max_attempts = (config == NULL) ? 8u : config->max_attempts;
    if (base_ms == 0u || cap_ms == 0u || max_attempts == 0u) {
        return NULL;
    }

    self = (pb_retry_exponential_full_jitter_state *)pb_alloc(
        allocator, sizeof(pb_retry_exponential_full_jitter_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->base_ms = base_ms;
    self->cap_ms = cap_ms;
    self->max_attempts = max_attempts;

    if (rng != NULL) {
        self->rng = rng;
    } else {
        /* Seeded rather than left absent: a policy constructed without one
         * still has to produce a delay, and an unseeded default would be a
         * global source by another name. */
        pb_rng_init(&self->owned, 0u);
        self->rng = &self->owned;
    }

    return (pb_retry *)self;
}

static void full_jitter_destroy(pb_retry *policy)
{
    pb_retry_exponential_full_jitter_state *self =
        (pb_retry_exponential_full_jitter_state *)policy;

    if (self == NULL) {
        return;
    }
    /* `rng` is borrowed when the caller supplied one, and points into this
     * struct otherwise. Either way there is nothing separate to free. */
    pb_free(self->allocator, self, sizeof(pb_retry_exponential_full_jitter_state));
}

static int64_t full_jitter_next_delay(pb_retry *policy, uint32_t attempt,
                                      const pb_retry_error *error)
{
    pb_retry_exponential_full_jitter_state *self =
        (pb_retry_exponential_full_jitter_state *)policy;
    uint32_t ceiling;

    assert(self != NULL);
    assert(error != NULL);

    /* Both refusals come before the draw, so a call this policy declines leaves
     * its random stream exactly where it was. A port that ordered these
     * differently would diverge from the first give-up onward. */
    if (!error->retryable) {
        return PB_RETRY_GIVE_UP;
    }
    if (attempt >= self->max_attempts) {
        return PB_RETRY_GIVE_UP;
    }

    /* `next_int(n)` returns 0..n-1, so the bound is the ceiling plus one and
     * the ceiling itself remains reachable. Zero is reachable too, and that is
     * deliberate: some client retrying immediately is what makes the arrival
     * pattern smooth rather than merely delayed.
     *
     * At a ceiling of UINT32_MAX the bound is 2^32: the reference's rejection
     * threshold is zero there and the draw is one raw 32-bit word, which is
     * what `next_u32` returns. `ceiling + 1u` would wrap the bound to zero. */
    ceiling = exponential_full_jitter_backoff_ceiling(attempt, self->base_ms, self->cap_ms);
    if (ceiling == UINT32_MAX) {
        return (int64_t)pb_rng_next_u32(self->rng);
    }
    return (int64_t)pb_rng_next_int(self->rng, ceiling + 1u);
}

static size_t full_jitter_memory_bytes(const pb_retry *policy)
{
    (void)policy;
    return sizeof(pb_retry_exponential_full_jitter_state);
}

const pb_retry_vtable pb_retry_exponential_full_jitter = { full_jitter_create,
                                                           full_jitter_next_delay,
                                                           full_jitter_memory_bytes,
                                                           full_jitter_destroy };

/* ========================================================================
 * src/retry/retry_after_aware.c
 * ======================================================================== */

/*
 * GENERATED COPY — do not edit. Edit policies/retry/retry-after-aware/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * RetryAfterAware — do what the server asked, and guess only when it did not.
 *
 * Mirrors index.ts and policy.py. The ceiling is restated here rather than
 * shared, for the same reason the other ports restate it: a policy file is
 * copied out of the registry whole.
 */




typedef struct pb_retry_retry_after_aware_state {
    const pb_allocator *allocator;
    pb_rng *rng;  /* borrowed from the caller */
    pb_rng owned; /* used only when the caller supplied none */
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;
} pb_retry_retry_after_aware_state;

/* min(cap, base * 2^(attempt - 1)), in integers and without overflowing. */
static uint32_t retry_after_aware_backoff_ceiling(uint32_t attempt, uint32_t base_ms, uint32_t cap_ms)
{
    /* 64-bit, as the float64 reference effectively is: with a cap above 2^31
     * the doubling that passes it would wrap a uint32 and dodge the clamp. */
    uint64_t delay = base_ms;
    uint32_t step;

    for (step = 1u; step < attempt; ++step) {
        if (delay >= cap_ms) {
            return cap_ms;
        }
        delay *= 2u;
    }
    return delay < cap_ms ? (uint32_t)delay : cap_ms;
}

static pb_retry *aware_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_retry_retry_after_aware_params *config =
        (const pb_retry_retry_after_aware_params *)params;
    pb_retry_retry_after_aware_state *self;
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;

    base_ms = (config == NULL) ? 100u : config->base_ms;
    cap_ms = (config == NULL) ? 10000u : config->cap_ms;
    max_attempts = (config == NULL) ? 8u : config->max_attempts;
    if (base_ms == 0u || cap_ms == 0u || max_attempts == 0u) {
        return NULL;
    }

    self = (pb_retry_retry_after_aware_state *)pb_alloc(
        allocator, sizeof(pb_retry_retry_after_aware_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->base_ms = base_ms;
    self->cap_ms = cap_ms;
    self->max_attempts = max_attempts;

    if (rng != NULL) {
        self->rng = rng;
    } else {
        pb_rng_init(&self->owned, 0u);
        self->rng = &self->owned;
    }
    return (pb_retry *)self;
}

static void aware_destroy(pb_retry *policy)
{
    pb_retry_retry_after_aware_state *self = (pb_retry_retry_after_aware_state *)policy;

    if (self == NULL) {
        return;
    }
    pb_free(self->allocator, self, sizeof(pb_retry_retry_after_aware_state));
}

static int64_t aware_next_delay(pb_retry *policy, uint32_t attempt,
                                const pb_retry_error *error)
{
    pb_retry_retry_after_aware_state *self = (pb_retry_retry_after_aware_state *)policy;
    uint64_t hint;
    uint32_t ceiling;

    assert(self != NULL);
    assert(error != NULL);

    if (!error->retryable) {
        return PB_RETRY_GIVE_UP;
    }
    if (attempt >= self->max_attempts) {
        return PB_RETRY_GIVE_UP;
    }

    if (error->has_retry_after) {
        /* No draw is consumed on this path. A port that drew anyway would leave
         * its stream in a different place and diverge on the next fallback. */
        hint = error->retry_after_ms;
        if (hint > (uint64_t)self->cap_ms) {
            hint = (uint64_t)self->cap_ms;
        }
        return (int64_t)hint;
    }

    /* The bound is the ceiling plus one. At a ceiling of UINT32_MAX that is
     * 2^32: the reference's rejection threshold is zero there and the draw is
     * one raw 32-bit word, which is what `next_u32` returns. `ceiling + 1u`
     * would wrap the bound to zero. */
    ceiling = retry_after_aware_backoff_ceiling(attempt, self->base_ms, self->cap_ms);
    if (ceiling == UINT32_MAX) {
        return (int64_t)pb_rng_next_u32(self->rng);
    }
    return (int64_t)pb_rng_next_int(self->rng, ceiling + 1u);
}

static size_t aware_memory_bytes(const pb_retry *policy)
{
    (void)policy;
    return sizeof(pb_retry_retry_after_aware_state);
}

const pb_retry_vtable pb_retry_retry_after_aware = { aware_create, aware_next_delay,
                                                     aware_memory_bytes, aware_destroy };

/* ========================================================================
 * src/retry/traces.c
 * ======================================================================== */

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

/* ========================================================================
 * src/rng.c
 * ======================================================================== */

#define PB_GAMMA 0x9E3779B9u
#define PB_MUL_1 0x21F0AAADu
#define PB_MUL_2 0x735A2D97u

/* 2^32 as a double. Exact. */
#define PB_TWO_POW_32 4294967296.0

/* The splitmix32 finalising mix. */
static uint32_t pb_finalise(uint32_t z)
{
    z ^= z >> 16;
    z *= PB_MUL_1;
    z ^= z >> 15;
    z *= PB_MUL_2;
    z ^= z >> 15;
    return z;
}

static uint32_t pb_rotl(uint32_t x, unsigned k)
{
    return (uint32_t)((x << k) | (x >> (32u - k)));
}

uint32_t pb_mix32(uint32_t value)
{
    return pb_finalise(value);
}

void pb_rng_init(pb_rng *rng, uint32_t seed)
{
    uint32_t state = seed;
    int i;

    assert(rng != NULL);

    /* splitmix32 expands the single seed word into the four state words. */
    for (i = 0; i < 4; ++i) {
        state += PB_GAMMA;
        rng->s[i] = pb_finalise(state);
    }

    /* xoshiro cannot start from all zeroes; it would emit zeroes forever. */
    if ((rng->s[0] | rng->s[1] | rng->s[2] | rng->s[3]) == 0u) {
        rng->s[0] = 1u;
    }
}

uint32_t pb_rng_next_u32(pb_rng *rng)
{
    uint32_t s1 = rng->s[1];
    uint32_t result;
    uint32_t t;
    uint32_t s2;
    uint32_t s3;

    /* The "**" scrambler: rotl(s1 * 5, 7) * 9. */
    result = pb_rotl(s1 * 5u, 7u) * 9u;

    t = (uint32_t)(s1 << 9);
    s2 = rng->s[2] ^ rng->s[0];
    s3 = rng->s[3] ^ s1;
    rng->s[1] = s1 ^ s2;
    rng->s[0] = rng->s[0] ^ s3;
    rng->s[2] = s2 ^ t;
    rng->s[3] = pb_rotl(s3, 11u);

    return result;
}

double pb_rng_next_float(pb_rng *rng)
{
    return (double)pb_rng_next_u32(rng) / PB_TWO_POW_32;
}

uint32_t pb_rng_next_int(pb_rng *rng, uint32_t bound)
{
    uint32_t threshold;
    uint32_t value;

    assert(bound >= 1u);

    /*
     * Discard the short tail so the remaining range is an exact multiple of
     * bound. (0u - bound) is 2^32 - bound in unsigned arithmetic.
     */
    threshold = (0u - bound) % bound;
    value = pb_rng_next_u32(rng);
    while (value < threshold) {
        value = pb_rng_next_u32(rng);
    }
    return value % bound;
}

/* ========================================================================
 * src/zipf.c
 * ======================================================================== */

double pb_zipf_weight(uint32_t rank, double alpha)
{
    double r = (double)rank + 1.0;

    if (alpha == 1.0) {
        return 1.0 / r;
    }

    /* r^0.75 = sqrt(r) * sqrt(sqrt(r)) — two correctly rounded operations. */
    {
        double s = sqrt(r);
        double q = sqrt(s);
        return 1.0 / (s * q);
    }
}

bool pb_zipf_init(pb_zipf *zipf, uint32_t size, double alpha, const pb_allocator *allocator)
{
    double running = 0.0;
    uint32_t rank;

    assert(zipf != NULL);

    zipf->cumulative = NULL;
    zipf->total = 0.0;
    zipf->size = 0;

    if (size == 0u) {
        return false;
    }
    if (alpha != 1.0 && alpha != 0.75) {
        /*
         * Other exponents would need pow, which is not correctly rounded across
         * C standard libraries and would break trace reproducibility.
         */
        return false;
    }

    zipf->cumulative = (double *)pb_alloc(allocator, (size_t)size * sizeof(double));
    if (zipf->cumulative == NULL) {
        return false;
    }

    /*
     * Summed in ascending rank order, which fixes the floating-point result.
     * Any other order gives a slightly different total, and eventually a
     * different sampled key.
     */
    for (rank = 0; rank < size; ++rank) {
        running += pb_zipf_weight(rank, alpha);
        zipf->cumulative[rank] = running;
    }

    zipf->total = running;
    zipf->size = size;
    return true;
}

void pb_zipf_destroy(pb_zipf *zipf, const pb_allocator *allocator)
{
    if (zipf == NULL || zipf->size == 0u) {
        return;
    }
    pb_free(allocator, zipf->cumulative, (size_t)zipf->size * sizeof(double));
    zipf->cumulative = NULL;
    zipf->total = 0.0;
    zipf->size = 0;
}

uint32_t pb_zipf_sample(const pb_zipf *zipf, pb_rng *rng)
{
    double target;
    uint32_t low = 0;
    uint32_t high;

    assert(zipf != NULL);
    assert(zipf->size > 0u);

    target = pb_rng_next_float(rng) * zipf->total;
    high = zipf->size - 1u;

    while (low < high) {
        uint32_t mid = (low + high) / 2u;
        if (zipf->cumulative[mid] > target) {
            high = mid;
        } else {
            low = mid + 1u;
        }
    }
    return low;
}

#endif /* POLICYBOOK_AMALGAMATED_IMPLEMENTATION */
#endif /* POLICYBOOK_IMPLEMENTATION */

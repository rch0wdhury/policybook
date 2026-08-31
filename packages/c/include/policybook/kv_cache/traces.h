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

#include <stddef.h>
#include <stdint.h>

#include "policybook/allocator.h"
#include "policybook/rng.h"

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

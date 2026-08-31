# Bélády OPT

OPT evicts whatever will be needed furthest in the future. You cannot deploy it:
it requires the entire future access sequence, which no real cache has.

It is here because it is the **bound**. Bélády proved in 1966 that furthest-next-use
is optimal, so no online policy can beat it on the same trace. That makes the
gap between a policy's hit rate and OPT's the honest measure of what is still on
the table, and, just as usefully, of when there is nothing left to win.

On the registry's Zipf trace, OPT reaches 0.809 where [SIEVE](../sieve/) reaches
0.727 and [LRU](../lru/) 0.675. Those eight points between SIEVE and OPT are the
entire remaining prize for that workload. A policy claiming to beat SIEVE by
twenty is claiming something impossible.

## When to use it

- In a benchmark, as the reference line. Every domain table in this registry
  carries one where an offline optimum exists.
- When deciding whether to keep optimising. If your policy is within two points
  of OPT, the remaining engineering is better spent elsewhere.
- When validating a harness. A policy that beats OPT on the same trace is proof
  of a bug in the harness, the trace, or the policy. This is a useful assertion
  to keep in a test suite, and this repository does.

## When not to use it

- In production, ever. It is offline by definition.
- As a target to approximate directly. "Predict the future" is not an
  implementable strategy. The useful policies approximate it *indirectly*, via
  recency ([LRU](../lru/)), frequency ([LFU](../lfu/)), or both
  ([ARC](../arc/)).
- On a trace you have not fully materialised. OPT needs the whole sequence in
  memory, which for a billion-event trace is a real cost that online policies do
  not pay.
- As a claim about achievable hit rate. OPT knows the future. The gap it shows
  is an upper bound on what *any* policy could win, not a target any policy can
  reach.

## How it works

One backward pass over the trace records, for each position, where that key is
next used. Eviction then takes the maximum over resident entries.

```
setup:
    for each position from the end backwards:
        nextUseAt[position] = the next position holding the same key,
                              or the trace length if there is none

onAccess(key, hit):
    check that the trace still matches the supplied future
    nextUse[key] = nextUseAt[step]; step += 1

evict():
    return the resident key with the greatest nextUse
```

Using the trace length as "never again" keeps every comparison an integer one:
it is strictly greater than any real position, so no infinity is needed.

The maximum is kept in an indexed binary heap, so a hit updates a key's priority
in place and an eviction costs O(log n) rather than a scan over the cache.

**Tie-breaking.** Keys never used again all share the same sentinel. Among them
the earliest inserted is evicted, which makes the choice determined rather than
an accident of heap layout.

## Why this differs from the textbook

The standard operating-systems treatment of optimal replacement runs the
reference string `7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1` with three frames and
reports **nine** page faults. This implementation reports **eight** on the same
input, and both are right.

The textbook models *demand paging*, where a referenced page must be brought
into memory. A cache is under no such obligation: it may decline to admit a key
at all, which is exactly what [2Q](../2q/) and [W-TinyLFU](../w-tinylfu/) do.
Given that freedom, the optimum is to refuse key `4`, which appears once and
never returns, instead of evicting something useful to make room for it.

Since the policies in this domain are permitted to decline admission, the
bypassing optimum is the correct bound for them. A no-bypass OPT would be a
*weaker* bound, and an admission-controlling policy measured against it could
appear to approach optimality more closely than it really does. The
`distinguishing` vector walks this exact case.

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
| `capacity` | number | 1000 | Maximum number of entries held. |
| `future` | array | `[]` | The complete access sequence, in order. Supplied by the harness. |

OPT checks each access against the supplied future and refuses to continue on a
mismatch. A bound computed against the wrong trace is not a bound, and failing
loudly is better than reporting a plausible number.

## Complexity

O(log n) per access and per eviction, where n is the capacity: the indexed heap.
Setup is one linear pass over the trace.

O(n + m) space, where m is the trace length: the cache's own entries, plus one
integer per trace position for the precomputed next-use table. That table is the
real cost, and it is why OPT is a benchmarking tool rather than a library.

## Benchmark

<!-- bench:start -->
| Trace | Hit rate | Evictions | Throughput |
|---|---:|---:|---:|
| `zipf-1.0-100k` | 0.8087 | 18,133 | 7,840,000/s |
| `zipf-0.75-1m` | 0.6399 | 350,137 | 4,340,000/s |
| `scan-heavy` | 0.7473 | 26,290 | 7,050,000/s |
| `shifting-popularity` | 0.7939 | 19,614 | 7,840,000/s |

Offline bound: this row is what no online policy can exceed, not a result to compare against.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

Bélády, *A study of replacement algorithms for a virtual-storage computer*, IBM
Systems Journal, 1966. Usually written MIN or OPT. The two names refer to the
same rule.

Related: every other policy in this domain is an attempt to approximate this one
without seeing the future.

## Notes

No patents.

**No C port.** OPT exists only as a benchmark reference line, and the canonical
`bench.json` is produced by the TypeScript implementation.
The `future` array has no natural expression in a C params struct, and adding
one would complicate the vector generator for a policy that will never run in a
C program. Its status is `offline-bound` rather than `stable`, which is the
status the catalog defines for exactly this case.

**It is not a "hit rate ceiling" for a real system.** OPT bounds what is
achievable on *this trace with this capacity*. A larger cache, a different
admission policy or a different workload shifts the bound. It is a measuring
stick, not a law of nature.

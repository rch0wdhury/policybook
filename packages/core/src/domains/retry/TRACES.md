# Retry episodes

The canonical workload every retry policy is benchmarked on. It is generated,
not downloaded: a port in any language reproduces it from the seed alone.

This document is the specification. `traces.ts` and `harness.ts` are the
reference implementation, and the Python and C ports must agree with them
exactly — the parity tests check the outage duration of every episode against a
committed reference.

## What an episode is

Not a stream of events, but an independent **outage**. Something broke at t=0, a
client attempts immediately, and the policy decides how long to wait before each
further attempt. The episode ends when an attempt succeeds, the policy gives up,
or the deadline passes.

## `outage-30s` — seed 60

1,000 episodes. Per episode:

```
environment = Rng(60 + episode)
policy_rng  = Rng(60 + 1_000_000 + episode)

d = environment.nextInt(30_001)          # outage duration, ms
now = 0
attempt = 1
loop:
    roll = environment.nextFloat()       # drawn on EVERY attempt
    if now >= d and roll < 0.9:
        success at `now`, after `attempt` attempts
    error = {status: 503, retryable: true,
             retryAfterMs: min(max(d - now, 0), 5000)}
    delay = policy.nextDelay(attempt, error, policy_rng)
    if delay is null: give up
    now += delay
    if now > 60_000: give up (deadline)
    attempt += 1
```

**Two random streams per episode, not one.** This is the one place the
implementation departs from the plan's wording, which pinned a single stream
ordered "outage draw, then per-attempt success draw, then policy's `nextDelay`
draws". With one stream, a policy that draws jitter shifts the position of every
later success roll, so two policies would face *different luck* rather than
different strategies — and the whole point of the benchmark is to compare
strategies. Splitting them costs nothing in determinism (both streams are seeded
from the episode index) and makes every policy face the identical outage and the
identical sequence of coin flips.

**The success roll is drawn on every attempt**, whether or not the outage has
passed. Drawing only when it could matter would make the stream position depend
on the outage, so two policies making the same number of attempts would see
different rolls. As written, attempt *k* always consumes environment draw *k*.

**One attempt in ten still fails after recovery.** A service that has just come
back is not instantly healthy, and a benchmark where the first post-outage
attempt always succeeds would flatter every policy equally and distinguish none.

## The reference configuration

Every policy is benchmarked at `baseMs` 100, `capMs` 10,000, `maxAttempts` 8.

That budget covers about 12.7 seconds of un-jittered exponential backoff against
outages of up to 30, so **most episodes end in failure for every policy**. That
is deliberate and it is the honest result: eight attempts at a 100 ms base is
what a default HTTP client does, and it does not survive a 30-second outage.
The interesting comparison is not who reaches 100% — nobody does — but what each
policy spends getting where it gets.

## What is measured

- **`successRate`** — episodes that ended in success. On its own it ranks
  nothing: a policy that waits longer scores higher, and a policy that retried
  forever would score 1.0 while being the reason the service stayed down.
- **`meanTimeToSuccessMs`** — over successful episodes only. The client-side
  cost: how long a user waited.
- **`meanAttempts`** — over all episodes, failures included. The server-side
  cost: how much work one client made the service do.
- **`p99Attempts`** — nearest-rank, no interpolation. The tail is what matters
  during an incident: the mean says what a typical client costs, this says what
  the worst hundredth cost.
- **`peakRetryShare`** — the largest share of all retries landing inside any
  **10 ms** window. This is the thundering-herd measure and the reason the
  domain exists.

### On `peakRetryShare`

Every episode's first attempt is at t=0, so the thousand episodes are also a
thousand clients that failed simultaneously — which is the situation jitter is
for. A policy with no randomness places every client's *n*-th retry at exactly
the same instant, so its spike stays large however gentle its delay curve looks
on paper.

**The window size is load-bearing.** It must be small relative to the first
delay, or it cannot separate "every client at exactly t=100" from "clients
spread across [0, 100]". At a 100 ms window the two are indistinguishable and
full jitter scores *worse* than plain exponential, because its early draws
cluster near zero. At 10 ms — a tenth of the reference base — full jitter scores
about six times better, which is the truth the metric is there to tell.

## The committed reference

`trace-prefix.json` holds the outage duration of each episode: the first draw of
each episode's environment stream, and so exactly what the harness sees. If a
port's Rng or its seeding differed, these numbers would differ first, and the
parity test says which episode.

# Retry-After aware

Every other policy in this domain is guessing. This one reads the answer when
the server has provided it: a `Retry-After` header is the service's own
statement of when it expects to be ready. When the header is absent it falls
back to [full jitter](../exponential-full-jitter/), so it is never worse than
the default.

```
if the error carries a Retry-After:  delay = min(cap, retryAfterMs)
otherwise:                           delay = rng.nextInt(ceiling + 1)
```

On the canonical workload it succeeds **99.6%** of the time against full
jitter's 19.9%, because it is told how long the outage will last instead of
inferring it. It also records the **worst herd of any policy here**: 23.4% of
all retries in a single 10 ms window, worse than doing no jittering at all.
Both numbers are real and both matter.

## When to use it

- **Against a service whose `Retry-After` you believe.** If the server computes
  it from a token bucket or a known recovery time, it has information you do
  not, and honouring it is strictly better than guessing.
- **Against APIs that rate-limit you.** `429 Too Many Requests` with a
  `Retry-After` is the standard shape, and the header is usually exact. Ignoring
  it means being refused again for no reason.
- **As a drop-in upgrade to full jitter.** The fallback is full jitter exactly,
  and a test checks the two agree call for call when no hint arrives, so
  adopting this can only change behaviour on responses that carried a hint.
- **When you need the highest success rate and control the server too.** If both
  ends are yours, a `Retry-After` derived from real readiness plus a per-client
  jitter *on the server side* gets the best of both.

## When not to use it

- **When the server's hint is a constant under load.** Many services return a
  fixed `Retry-After` when overwhelmed, which lands every client on the same
  millisecond. That is the herd this whole domain exists to prevent, and this
  policy walks straight into it. Prefer
  [full jitter](../exponential-full-jitter/) against a service you do not
  control.
- **When you cannot bound the wait.** Without a sensible `capMs` you have handed
  a stranger control of your latency budget.
- **For a large fleet behind one dependency.** The more clients share a server,
  the more a shared hint costs you, and the more the alternative's spreading is
  worth.
- **When the hint is unreliable.** A wrong hint is worse than no hint: it is
  confidently wrong, and the policy has no way to tell.

## How it works

The clamp and the fallback are the whole implementation, but three details are
load-bearing.

**An absent hint and a hint of zero are different statements.** Zero means
"come back now" and is honoured. The check is for `undefined` rather than for
falsiness. A vector pins both.

**No random draw is consumed on the hinted path.** A port that drew anyway
would leave its stream in a different place and diverge on the next fallback.
Two vector cases prove it: they use the same seed, interleave different numbers
of hinted calls, and produce the identical fallback draws (375, then 9).

**The hint is validated.** A negative or fractional `retryAfterMs` throws rather
than becoming a strange delay, because a malformed header is a bug in the
caller's parsing and should surface where it happened.

## Why the herd number is so bad, and why the benchmark *understates* it

835 of the 1,000 episodes have an outage longer than the harness's five-second
cap on what a server will ask for. All 835 therefore receive the identical hint
and retry at exactly the same instant. **The clamp itself creates the herd**,
and that is not an artefact: real services cap their `Retry-After` the same way,
so real fleets cluster on the cap the same way.

And the benchmark is *kind* to this policy. Each episode has its own outage
duration, so the hints vary between episodes that fall under the cap. In
production a fleet shares one outage and receives one hint, so every client
would land together rather than only the 835 that hit the cap. Read 23.4% as a
floor.

If you need both the accuracy and the spreading, jitter the hint yourself:
`min(cap, retryAfterMs) + rng.nextInt(spread)`. This policy deliberately does
not, because the moment it modifies the server's answer it is guessing again,
and the registry would rather ship the honest version and describe the trade.

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
| `baseMs` | number | 100 | The first fallback ceiling, doubled on each subsequent attempt. |
| `capMs` | number | 10000 | The longest wait this client will accept, from any source. |
| `maxAttempts` | number | 8 | Give up after this many attempts. |

## Complexity

O(attempt) time on the fallback path, where a bounded loop stops at the cap,
and O(1) on the hinted path. O(1) space.

## Benchmark

<!-- bench:start -->
| Trace | Success rate | Peak retry share | Throughput |
|---|---:|---:|---:|
| `outage-30s` | 0.9960 | 0.2335 | 3,650,000/s |

Success rate is what a client gets. Peak retry share is what the recovering service pays, and the two trade against each other. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

`Retry-After` is defined by RFC 9110 §10.2.3, which specifies both the
delay-seconds and HTTP-date forms. This policy takes milliseconds, leaving the
parsing to the caller, a decision about where a header ends and a policy begins.

Its nearest neighbour is [full jitter](../exponential-full-jitter/), which it
becomes exactly when no hint arrives. The comparison is the clearest statement
of the trade in this domain: reading the server's answer buys a fivefold
increase in success rate and gives back everything jitter was protecting.

## Notes

No patents known.

The `Rng` is supplied at construction rather than passed to `nextDelay` (see the
domain interface for why).

Callers that receive `Retry-After` as an HTTP-date must convert it themselves.
Doing that conversion inside a policy would mean reading a clock, which nothing
in this registry does.

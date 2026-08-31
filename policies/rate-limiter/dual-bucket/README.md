# Dual bucket

Two limits at once: **requests per minute** and **tokens per minute**, checked
together, and a call must satisfy both. This is the shape every LLM API uses,
and increasingly every metered API: one dimension counts calls, the other
counts how much work each call asks for.

The two fail in opposite directions, which is the point of having both. A flood
of tiny requests exhausts RPM while TPM sits nearly untouched: that is a client
stuck in a retry loop, and the request ceiling is what stops it. A single
enormous request exhausts TPM while RPM has barely moved: that is a caller
submitting a book, and the token ceiling is what stops it. A limiter with only
one of these is blind to half the ways it can be overwhelmed.

Each dimension is a [token bucket](../token-bucket/) with a per-minute period,
so everything true of that policy is true of each half here.

## When to use it

- **In front of an LLM API, or behind one.** RPM plus TPM is the near-universal
  convention, and matching it means your limits are legible to callers who have
  already read someone else's docs.
- **Any metered service where request count and request size both cost you.**
  Storage APIs charged per operation and per byte, batch endpoints, anything
  where a caller can choose to send one big thing or many small ones.
- **When you want a caller's retry loop to be self-limiting.** The request
  ceiling bites first on retries, and because the charge is atomic a refused
  call costs the caller nothing extra.
- **When quoting limits to customers.** "500 requests and 200,000 tokens a
  minute" is a sentence a customer can plan against.

## When not to use it

- **When only one dimension actually costs you anything.** Two limits mean two
  numbers to tune and two ways to be wrong. If request size is irrelevant, a
  plain [token bucket](../token-bucket/) is the honest configuration.
- **When the work a call will do is not known before you admit it.** The token
  charge has to be an estimate at admission time: an LLM API charges input
  tokens up front and reconciles output afterwards. If you cannot estimate at
  all, this policy is measuring something you have not got.
- **When you need more than two dimensions.** Nothing here generalises. A third
  limit means a third pair of fields. At that point the answer is usually a
  small array of buckets rather than this policy.
- **When you need an exact "N in any window" guarantee.** Like the token bucket,
  each dimension admits up to `rate x window + ceiling` in a window.

## How it works

Each key holds two independent ledgers and one timestamp.

```
refill both dimensions from (now - last), clamped to one minute
if requests < 1:    return false
if tokens   < cost: return false
requests -= 1
tokens   -= cost
return true
```

**The charge is atomic.** If either dimension would refuse, neither is charged.
Getting this wrong is a real and easy bug: it turns a client that retries into a
client that is throttled harder for retrying. A vector pins it: after a call
refused for tokens, the request count is checked to be unchanged. A property
test replays 2,000 randomised calls asserting that a refusal moves neither
counter and an admission moves both by exactly the right amount.

**Each dimension is the token bucket's integer ledger with a period of a minute
rather than a second.** The fraction lives in a credit accumulator measured in
sixty-thousandths of a permit, so nothing is rounded away. Elapsed time is
clamped to one period, which is exactly how long a drained bucket takes to
refill: beyond that the result cannot change, and clamping keeps the multiply
bounded in the C port.

**They refill independently, and that is visible.** Ten seconds after a key
spends everything at 3 requests and 1,000 units a minute, the work ceiling has
recovered 166 units while the request ceiling has not yet earned a single call.
A vector walks exactly that.

**`retryAfter` reports the later of the two**, because a caller must satisfy
both. It assumes a smallest possible call, one request and one token, since the
interface has no cost argument, so a large call may still be refused after it
elapses. That limitation is stated rather than hidden. A caller with a specific
request in mind should ask again.

**Tie-breaking.** A call costing exactly the remaining work units is admitted:
the comparison is `tokens < cost`, so equality passes.

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
| `requestsPerMin` | number | 500 | Calls allowed per minute, regardless of size. |
| `tokensPerMin` | number | 200000 | Units of work allowed per minute, summed over calls. |

The defaults are a plausible mid-tier LLM API allowance rather than the domain's
100-per-second reference, because per-minute pairs are how these limits are
actually published and a per-second default would misrepresent the shape.

## Complexity

O(1) per request: one hash lookup and two ledger updates. Space is five
integers per tracked key: two balances, two carries, and one timestamp.

## Benchmark

<!-- bench:start -->
| Trace | Accept rate | Peak / 100 ms | Throughput |
|---|---:|---:|---:|
| `steady` | 1 | 22 | 45,200,000/s |
| `bursty` | 1 | 62 | 57,100,000/s |
| `many-keys` | 1 | 69 | 24,600,000/s |
| `overload` | 0.6747 | 49 | 49,500,000/s |

Accept rate alone ranks nothing here: under sustained overload every correct limiter admits rate times time, so these converge by construction. Read the domain README, because the choice is made on memory, on behaviour at a window seam, and on what distributes. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

Folklore, and recent: the RPM-plus-TPM pair became a convention through LLM API
documentation rather than through any paper. It is a composition of two token
buckets, not a new algorithm, and this page exists because the *composition* is
what people need, including the atomicity rule, which is the part most
re-implementations get wrong.

Its nearest neighbour is [token bucket](../token-bucket/), of which this is
literally two. If you find yourself configuring one of the two dimensions to be
effectively unlimited, use that instead.

## Notes

No patents known.

Keys are integers: callers with string keys hash them, and `mix32` is the
registry's default.

The C port takes an extra `max_keys` parameter and refuses keys beyond it, since
it allocates everything up front and never grows. That is a property of the C
API rather than of the policy, and it is documented in `dual_bucket.h`.

# policybook

Runnable decision policies for systems and AI infrastructure — cache eviction,
rate limiting, retries, KV-cache eviction — with a one-page explainer, test
vectors and benchmark numbers behind every entry.

```python
from policybook.cache import Sieve

cache = Sieve(capacity=1000)
cache.on_access("k", hit=False)
victim = cache.evict()
```

Pure Python, standard library only, typed (`py.typed`), 3.10+.

## What is in it

Thirty policies across four domains, each in three languages:

| Domain | Policies | Start with |
|---|---|---|
| `policybook.cache` | FIFO, LRU, LFU, CLOCK, SIEVE, 2Q, ARC, W-TinyLFU, S3-FIFO, Bélády OPT | `Sieve` |
| `policybook.rate_limiter` | fixed window, sliding log, sliding counter, token bucket, leaky bucket, GCRA, dual bucket | `TokenBucket` |
| `policybook.retry` | constant, exponential, full jitter, equal jitter, decorrelated jitter, Retry-After-aware | `ExponentialFullJitter` |
| `policybook.kv_cache` | sliding window, StreamingLLM, H2O, Scissorhands, TOVA, SnapKV, PyramidKV | `StreamingLlm` |

```python
from policybook.rate_limiter import TokenBucket
from policybook.retry import ExponentialFullJitter
from policybook.kv_cache import StreamingLlm
```

Each domain also exports its canonical trace generators and its interface, so
you can benchmark a policy of your own against the same workloads:

```python
from policybook.cache import CACHE_TRACES, generate_cache_trace
```

## Why the numbers agree

Every policy is checked against the same language-neutral test vectors as the
TypeScript and C implementations, so a decision made here matches a decision
made there — exactly, not approximately. The trace generators are checked the
same way, against a committed reference, which is what makes a benchmark run in
Python comparable with one run in C.

The KV-cache traces are floating-point, and are compared on **float32 bit
patterns** rather than values, because two floats that print alike can differ in
the last place.

## Not a framework

There is no runtime, no registry lookup, no configuration system. A policy is a
class with two or three methods and no dependencies, and the intended way to use
one is often to copy it into your own project:

```bash
npx policybook add cache/sieve --lang python
```

That emits a single self-contained file with any shared helper inlined. A test
in this repository copies each of four policies into an empty directory and
replays its vectors there with nothing else installed, so the claim is checked
rather than asserted.

MIT licensed. Full documentation, benchmarks and the reasoning behind each
policy: <https://github.com/policybook/policybook>

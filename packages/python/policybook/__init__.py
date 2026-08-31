"""Policybook — runnable decision policies for systems and AI infrastructure.

Cache eviction, rate limiting, retries, KV-cache eviction: all the same shape of
problem, a small function deciding from local state. Every policy here is
implemented against a tiny interface, documented in one page, and checked
against language-neutral test vectors shared with the TypeScript and C
implementations — so a decision made here matches a decision made there exactly,
not approximately.

    from policybook.cache import Sieve

    cache = Sieve(capacity=1000)
    cache.on_access("k", hit=False)
    victim = cache.evict()

Four domains, each importable on its own:

    from policybook.cache import Sieve, Lru, Arc
    from policybook.rate_limiter import TokenBucket, Gcra
    from policybook.retry import ExponentialFullJitter
    from policybook.kv_cache import StreamingLlm, H2o

Each also exports its canonical trace generators, so a policy of your own can be
benchmarked against the same workloads the published numbers come from.

Standard library only. Nothing here reads a file, a clock, or an environment
variable: time and randomness are always supplied by the caller, because a
policy that reached for them could not be tested.
"""

from policybook.rng import Rng, mix32

__all__ = ["Rng", "__version__", "mix32"]

__version__ = "0.1.0a1"
"""The distribution version.

An alpha of the 0.1.0 that `packages/core` reports as ``CORE_VERSION``: the two
move together, and ``tests/test_package.py`` fails if they stop agreeing.
"""

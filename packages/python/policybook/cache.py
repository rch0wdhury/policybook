"""The `cache` domain: 10 policies.

    from policybook.cache import Arc

GENERATED — do not edit. Regenerate with:
    pnpm tsx scripts/assemble-python.ts
"""

from __future__ import annotations

from policybook.domains.cache import (
    CACHE_TRACES,
    Arc,
    CacheMeta,
    CachePolicy,
    CacheTraceSpec,
    Clock,
    Fifo,
    Lfu,
    Lru,
    Opt,
    S3Fifo,
    Sieve,
    TwoQueue,
    WTinyLfu,
    generate_cache_trace,
)

__all__ = [
    "CACHE_TRACES",
    "Arc",
    "CacheMeta",
    "CachePolicy",
    "CacheTraceSpec",
    "Clock",
    "Fifo",
    "Lfu",
    "Lru",
    "Opt",
    "S3Fifo",
    "Sieve",
    "TwoQueue",
    "WTinyLfu",
    "generate_cache_trace",
]

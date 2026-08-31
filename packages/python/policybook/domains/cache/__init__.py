"""The `cache` domain.

GENERATED — do not edit. Regenerate with:
    pnpm tsx scripts/assemble-python.ts
"""

from __future__ import annotations

from policybook.domains.cache.arc import Arc
from policybook.domains.cache.clock import Clock
from policybook.domains.cache.fifo import Fifo
from policybook.domains.cache.interface import CacheMeta, CachePolicy
from policybook.domains.cache.lfu import Lfu
from policybook.domains.cache.lru import Lru
from policybook.domains.cache.opt import Opt
from policybook.domains.cache.s3_fifo import S3Fifo
from policybook.domains.cache.sieve import Sieve
from policybook.domains.cache.traces import CACHE_TRACES, CacheTraceSpec, generate_cache_trace
from policybook.domains.cache.two_queue import TwoQueue
from policybook.domains.cache.w_tiny_lfu import WTinyLfu

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

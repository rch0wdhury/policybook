"""Canonical traces for the cache domain.

The Python side of ``packages/core/src/domains/cache/traces.ts``. The prose
specification both implementations answer to is ``TRACES.md`` next to the
TypeScript file; ``tests/test_trace_parity.py`` checks the first 10,000 events
of every trace against the committed reference.

Nothing here reads a file, a clock, or an environment variable.
"""

from __future__ import annotations

from dataclasses import dataclass

from policybook.rng import Rng
from policybook.zipf import ZipfSampler

__all__ = ["CACHE_TRACES", "CacheTraceSpec", "generate_cache_trace"]


@dataclass(frozen=True)
class CacheTraceSpec:
    """Everything needed to reproduce a trace and to benchmark against it."""

    id: str
    description: str
    keyspace: int
    """Number of distinct keys the Zipf body draws from."""
    key_universe: int
    """Exclusive upper bound on any key the trace emits."""
    capacity: int
    """Cache capacity this trace is benchmarked at."""
    events: int
    seed: int


CACHE_TRACES: dict[str, CacheTraceSpec] = {
    "zipf-1.0-100k": CacheTraceSpec(
        id="zipf-1.0-100k",
        description=(
            "100,000 accesses over 10,000 keys, Zipf alpha 1.0. The everyday skewed workload."
        ),
        keyspace=10_000,
        key_universe=10_000,
        capacity=1_000,
        events=100_000,
        seed=42,
    ),
    "zipf-0.75-1m": CacheTraceSpec(
        id="zipf-0.75-1m",
        description=(
            "1,000,000 accesses over 100,000 keys, Zipf alpha 0.75. Flatter, and large "
            "enough to punish per-entry overhead."
        ),
        keyspace=100_000,
        key_universe=100_000,
        capacity=10_000,
        events=1_000_000,
        seed=43,
    ),
    "scan-heavy": CacheTraceSpec(
        id="scan-heavy",
        description=(
            "Zipf 1.0 with a sequential scan of twice the capacity injected every 20,000 "
            "accesses. Separates scan-resistant policies from LRU."
        ),
        keyspace=10_000,
        # The scans emit keys above the Zipf keyspace: 10,000 .. 17,999.
        key_universe=18_000,
        capacity=1_000,
        events=108_000,
        seed=44,
    ),
    "shifting-popularity": CacheTraceSpec(
        id="shifting-popularity",
        description=(
            "Zipf 1.0 whose popular set rotates every 25,000 accesses. Punishes policies "
            "that cannot forget."
        ),
        keyspace=10_000,
        key_universe=10_000,
        capacity=1_000,
        events=100_000,
        seed=45,
    ),
}

SCAN_INTERVAL = 20_000
"""How often the scan-heavy trace injects a scan."""

POPULARITY_SHIFT = 2_500
"""How far the popular set rotates each time, in keys."""

SHIFT_INTERVAL = 25_000
"""How often the popular set rotates."""


def _spec_for(trace_id: str) -> CacheTraceSpec:
    spec = CACHE_TRACES.get(trace_id)
    if spec is None:
        known = ", ".join(CACHE_TRACES)
        msg = f'unknown cache trace "{trace_id}". Known: {known}'
        raise KeyError(msg)
    return spec


def generate_cache_trace(trace_id: str, max_events: int | None = None) -> list[int]:
    """Generate a trace as a flat list of keys.

    Args:
        trace_id: one of ``CACHE_TRACES``.
        max_events: stop early after this many events. The prefix of a trace is
            identical to the full trace, so a short run is still reproducible.
    """
    spec = _spec_for(trace_id)
    limit = spec.events if max_events is None else min(max_events, spec.events)

    if trace_id == "zipf-1.0-100k":
        return _generate_zipf(spec, 1.0, limit)
    if trace_id == "zipf-0.75-1m":
        return _generate_zipf(spec, 0.75, limit)
    if trace_id == "scan-heavy":
        return _generate_scan_heavy(spec, limit)
    if trace_id == "shifting-popularity":
        return _generate_shifting_popularity(spec, limit)

    msg = f'no generator for trace "{trace_id}"'
    raise KeyError(msg)


def _generate_zipf(spec: CacheTraceSpec, alpha: float, limit: int) -> list[int]:
    """Plain Zipf: draw a rank, emit it as the key."""
    rng = Rng(spec.seed)
    zipf = ZipfSampler(spec.keyspace, alpha)
    sample = zipf.sample
    return [sample(rng) for _ in range(limit)]


def _generate_scan_heavy(spec: CacheTraceSpec, limit: int) -> list[int]:
    """Zipf with periodic sequential scans.

    Every ``SCAN_INTERVAL`` accesses, a run of ``2 * capacity`` fresh keys sweeps
    through — the shape of a backup, a table scan, or a crawler. LRU caches all
    of it and throws away its working set; a scan-resistant policy barely
    notices. Scan keys live above the Zipf keyspace so they can never collide
    with the working set.
    """
    rng = Rng(spec.seed)
    zipf = ZipfSampler(spec.keyspace, 1.0)
    scan_length = spec.capacity * 2

    trace: list[int] = []
    scan_index = 0

    for step in range(spec.events):
        if len(trace) >= limit:
            break

        if step > 0 and step % SCAN_INTERVAL == 0:
            scan_base = spec.keyspace + scan_index * scan_length
            for offset in range(scan_length):
                if len(trace) >= limit:
                    break
                trace.append(scan_base + offset)
            scan_index += 1
            if len(trace) >= limit:
                break

        trace.append(zipf.sample(rng))

    return trace


def _generate_shifting_popularity(spec: CacheTraceSpec, limit: int) -> list[int]:
    """Zipf whose popular set rotates.

    The rank drawn is the same as ever, but it is offset by a rotation that
    advances every ``SHIFT_INTERVAL`` accesses, so yesterday's hot keys go cold.
    Frequency-based policies that never forget keep the wrong entries.
    """
    rng = Rng(spec.seed)
    zipf = ZipfSampler(spec.keyspace, 1.0)

    trace: list[int] = []
    for step in range(limit):
        rank = zipf.sample(rng)
        shift = (step // SHIFT_INTERVAL) * POPULARITY_SHIFT
        trace.append((rank + shift) % spec.keyspace)
    return trace

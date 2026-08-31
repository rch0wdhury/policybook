"""Canonical traces for the rate-limiter domain.

The Python side of ``packages/core/src/domains/rate-limiter/traces.ts``. The
prose specification both implementations answer to is ``TRACES.md`` next to the
TypeScript file; ``tests/test_trace_parity.py`` checks the first 10,000 events
of every trace against the committed reference.

Arrivals are a per-millisecond Bernoulli process rather than a Poisson one: a
Poisson inter-arrival time needs a logarithm, ``log`` is not correctly rounded
across C standard libraries, and the three ports would eventually disagree.

Nothing here reads a file, a clock, or an environment variable.
"""

from __future__ import annotations

from dataclasses import dataclass

from policybook.rng import Rng
from policybook.zipf import ZipfSampler

__all__ = [
    "RATE_LIMITER_TRACES",
    "RateLimiterTrace",
    "RateLimiterTraceSpec",
    "generate_rate_limiter_trace",
]


@dataclass(frozen=True)
class RateLimiterTraceSpec:
    """Everything needed to reproduce a trace and to benchmark against it."""

    id: str
    description: str
    duration_ms: int
    """Length of the simulated period, in milliseconds."""
    key_universe: int
    """Exclusive upper bound on any key the trace emits."""
    seed: int


@dataclass(frozen=True)
class RateLimiterTrace:
    """A generated trace: arrival times and the key each arrival names.

    There is no per-event cost list: every canonical arrival costs one unit.
    Costs other than one are a real part of the interface but are exercised by
    hand-written vectors, where the numbers can be reasoned about.
    """

    times: list[int]
    """Arrival times in milliseconds, non-decreasing."""
    keys: list[int]
    """The key of each arrival, parallel to ``times``."""


RATE_LIMITER_TRACES: dict[str, RateLimiterTraceSpec] = {
    "steady": RateLimiterTraceSpec(
        id="steady",
        description=(
            "One key arriving at about 90 requests per second for a minute — just under a "
            "100/s limit. Separates policies that leak from policies that do not."
        ),
        duration_ms=60_000,
        key_universe=1,
        seed=50,
    ),
    "bursty": RateLimiterTraceSpec(
        id="bursty",
        description=(
            "One key, silent for 1,800 ms then 200 ms at 500 requests per second, "
            "repeating. The shape that separates a token bucket from a leaky bucket."
        ),
        duration_ms=60_000,
        key_universe=1,
        seed=51,
    ),
    "many-keys": RateLimiterTraceSpec(
        id="many-keys",
        description=(
            "Two minutes at 500 requests per second spread over 10,000 keys, Zipf alpha "
            "1.0. Exposes per-key bookkeeping cost and unfairness."
        ),
        duration_ms=120_000,
        key_universe=10_000,
        seed=52,
    ),
    "overload": RateLimiterTraceSpec(
        id="overload",
        description=(
            "One key at 300 requests per second for a minute — three times a 100/s limit, "
            "sustained. Every correct limiter admits the same total; what differs is the shape."
        ),
        duration_ms=60_000,
        key_universe=1,
        seed=53,
    ),
}

STEADY_P = 0.09
"""Arrival probability per millisecond on the ``steady`` trace."""

BURSTY_P = 0.5
"""Arrival probability per millisecond while ``bursty`` is in its ON phase."""

MANY_KEYS_P = 0.5
"""Arrival probability per millisecond on ``many-keys``."""

OVERLOAD_P = 0.3
"""Arrival probability per millisecond on ``overload`` — three times the limit."""

BURST_CYCLE_MS = 2_000
"""The ``bursty`` cycle length."""

BURST_ON_MS = 200
"""How much of each ``bursty`` cycle is ON."""

MANY_KEYS_KEYSPACE = 10_000
"""Distinct keys the ``many-keys`` Zipf body draws from."""


def _spec_for(trace_id: str) -> RateLimiterTraceSpec:
    spec = RATE_LIMITER_TRACES.get(trace_id)
    if spec is None:
        known = ", ".join(RATE_LIMITER_TRACES)
        msg = f'unknown rate-limiter trace "{trace_id}". Known: {known}'
        raise KeyError(msg)
    return spec


def generate_rate_limiter_trace(
    trace_id: str, max_events: int | None = None
) -> RateLimiterTrace:
    """Generate a trace.

    Args:
        trace_id: one of ``RATE_LIMITER_TRACES``.
        max_events: stop after this many arrivals. Generation is sequential and
            consumes the random stream in order, so a truncated trace is exactly
            a prefix of the full one.
    """
    spec = _spec_for(trace_id)

    if trace_id == "steady":
        return _generate_single_key(spec, STEADY_P, max_events)
    if trace_id == "bursty":
        return _generate_bursty(spec, max_events)
    if trace_id == "many-keys":
        return _generate_many_keys(spec, max_events)
    if trace_id == "overload":
        # The same shape as ``steady``, at a rate the limiter cannot meet.
        return _generate_single_key(spec, OVERLOAD_P, max_events)

    msg = f'no generator for trace "{trace_id}"'
    raise KeyError(msg)


def _generate_single_key(
    spec: RateLimiterTraceSpec, probability: float, max_events: int | None
) -> RateLimiterTrace:
    """A steady stream on a single key: one Bernoulli draw per millisecond.

    Shared by ``steady`` and ``overload``, which differ only in the arrival rate
    — one just under the reference limit and one three times over it.
    """
    rng = Rng(spec.seed)
    next_float = rng.next_float
    times: list[int] = []

    for t in range(spec.duration_ms):
        if max_events is not None and len(times) >= max_events:
            break
        if next_float() < probability:
            times.append(t)

    return RateLimiterTrace(times=times, keys=[0] * len(times))


def _generate_bursty(
    spec: RateLimiterTraceSpec, max_events: int | None
) -> RateLimiterTrace:
    """Silence, then a burst, repeating.

    A millisecond in the OFF phase consumes no random draw. That is the pinned
    call order and every port must match it: drawing during the silence would be
    equally valid as a definition and would produce a completely different trace
    from the same seed.
    """
    rng = Rng(spec.seed)
    next_float = rng.next_float
    times: list[int] = []

    for t in range(spec.duration_ms):
        if max_events is not None and len(times) >= max_events:
            break
        if t % BURST_CYCLE_MS >= BURST_ON_MS:
            continue
        if next_float() < BURSTY_P:
            times.append(t)

    return RateLimiterTrace(times=times, keys=[0] * len(times))


def _generate_many_keys(
    spec: RateLimiterTraceSpec, max_events: int | None
) -> RateLimiterTrace:
    """A busy stream spread over a skewed keyspace.

    The draw order is pinned: the Bernoulli draw comes first, and the Zipf
    sample is taken only when that draw produced an arrival. Sampling a key
    unconditionally would consume the stream at a different rate and diverge.
    """
    rng = Rng(spec.seed)
    zipf = ZipfSampler(MANY_KEYS_KEYSPACE, 1.0)
    next_float = rng.next_float
    sample = zipf.sample
    times: list[int] = []
    keys: list[int] = []

    for t in range(spec.duration_ms):
        if max_events is not None and len(times) >= max_events:
            break
        if next_float() < MANY_KEYS_P:
            times.append(t)
            keys.append(sample(rng))

    return RateLimiterTrace(times=times, keys=keys)

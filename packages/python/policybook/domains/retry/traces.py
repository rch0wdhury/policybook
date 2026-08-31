"""Canonical workloads for the retry domain.

The Python side of ``packages/core/src/domains/retry/traces.ts``. The prose
specification both implementations answer to is ``TRACES.md`` next to the
TypeScript file; ``tests/test_trace_parity.py`` checks the outage duration of
every episode against the committed reference.

A retry "trace" is not a stream of events but a set of independent episodes:
something broke, and a client keeps trying until it succeeds or gives up. What
varies between episodes is how long the outage lasts, so the trace is the
sequence of outage durations.

Nothing here reads a file, a clock, or an environment variable.
"""

from __future__ import annotations

from dataclasses import dataclass

from policybook.rng import Rng

__all__ = [
    "RETRY_TRACES",
    "RetryTraceSpec",
    "environment_seed",
    "generate_retry_trace",
    "policy_seed",
]


@dataclass(frozen=True)
class RetryTraceSpec:
    """Everything needed to reproduce an episode set."""

    id: str
    description: str
    episodes: int
    """Independent episodes simulated."""
    max_outage_ms: int
    """Exclusive upper bound on an outage, in milliseconds."""
    deadline_ms: int
    """An episode is abandoned once the clock passes this."""
    flake_percent: int
    """Probability an attempt made after the outage still fails, as a percent."""
    seed: int


RETRY_TRACES: dict[str, RetryTraceSpec] = {
    "outage-30s": RetryTraceSpec(
        id="outage-30s",
        description=(
            "1,000 independent outages of up to 30 seconds, behind a service that still "
            "fails one attempt in ten once recovered. The everyday transient failure."
        ),
        episodes=1_000,
        max_outage_ms=30_001,
        deadline_ms=60_000,
        flake_percent=10,
        seed=60,
    ),
}

POLICY_SEED_OFFSET = 1_000_000
"""Far enough from any environment seed that the two never collide."""


def environment_seed(spec: RetryTraceSpec, episode: int) -> int:
    """Seed for an episode's environment stream: the outage and success rolls.

    One stream per episode rather than one for the whole run, so episode 40
    faces the same outage whatever the policy did in episode 39.
    """
    return spec.seed + episode


def policy_seed(spec: RetryTraceSpec, episode: int) -> int:
    """Seed for an episode's policy stream: whatever jitter the policy draws.

    Deliberately separate from the environment's. A shared stream would let a
    policy's own draws shift the outcome of the environment's later coin flips,
    so two policies would face different luck rather than different strategies.
    """
    return spec.seed + POLICY_SEED_OFFSET + episode


def _spec_for(trace_id: str) -> RetryTraceSpec:
    spec = RETRY_TRACES.get(trace_id)
    if spec is None:
        known = ", ".join(RETRY_TRACES)
        msg = f'unknown retry trace "{trace_id}". Known: {known}'
        raise KeyError(msg)
    return spec


def generate_retry_trace(trace_id: str, max_events: int | None = None) -> list[int]:
    """The outage duration of each episode, in milliseconds.

    This is the first draw of each episode's environment stream, so it is
    exactly what the harness will see.
    """
    spec = _spec_for(trace_id)
    limit = spec.episodes if max_events is None else min(max_events, spec.episodes)

    return [
        Rng(environment_seed(spec, episode)).next_int(spec.max_outage_ms)
        for episode in range(limit)
    ]

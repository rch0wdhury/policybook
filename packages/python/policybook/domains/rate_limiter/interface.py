"""The ``rate-limiter`` domain interface.

Mirrors ``packages/core/src/domains/rate-limiter/interface.ts`` method for
method; only the spelling is idiomatic Python (concept.md §12.3).

A limiter is asked one question — may this request go through, right now? — and
the interesting part is what it does with the ones it refuses. A fixed window
lets through twice its limit at a window boundary. A token bucket absorbs bursts
by design. A leaky bucket refuses to. Which is correct depends entirely on what
is downstream.

Time is an integer number of milliseconds supplied by the caller: a policy never
reads the clock, because it could not be tested if it did. Every operation on
that time is integer arithmetic too — milli-token ledgers with an explicit carry
rather than floating-point token counts. Floats
drift, and three languages drift differently.
"""

from __future__ import annotations

from typing import Final, Protocol

__all__ = ["RATE_LIMITER_REFERENCE", "RateLimiterPolicy"]


class RateLimiterPolicy(Protocol):
    """Every rate-limiter policy implements this.

    A policy may additionally define ``retry_after(key, now) -> int`` (how long
    until the key could succeed, in milliseconds) and ``state_size() -> int``
    (how many keys it is tracking, for the memory metric). Both are
    optional, so neither is part of the protocol; the
    harness checks for them.
    """

    def allow(self, key: int, cost: int, now: int) -> bool:
        """May a request of ``cost`` units for ``key`` proceed at time ``now``?

        ``now`` is a non-decreasing integer in milliseconds. Returning true is
        the decision *and* the commitment: whatever budget the request consumes
        has been consumed by the time this returns, so a policy must not be
        asked speculatively.
        """
        ...


RATE_LIMITER_REFERENCE: Final[dict[str, int]] = {
    "permits_per_second": 100,
    "burst": 100,
}
"""The reference configuration every canonical benchmark uses.

Policies express their limits differently — permits per window, tokens per
second, an emission interval — so this is stated once in neutral terms and each
policy's README says how it maps onto its own parameters. Without a shared
reference the benchmark table would compare policies configured differently,
which is worse than no table at all.
"""

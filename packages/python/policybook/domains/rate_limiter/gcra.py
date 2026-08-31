# GENERATED COPY — do not edit. Edit policies/rate-limiter/gcra/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""Gcra — the token bucket, kept as one number instead of three.

The Generic Cell Rate Algorithm stores a single theoretical arrival time per
key: the instant at which the next request would be exactly on schedule. How
many permits are banked, and how much of the next one has accrued, are both
implied by how far that instant sits from now.

It admits and refuses exactly what the token bucket does, including the
fractional carry, with ``retry_after`` agreeing to the millisecond. The reason to
choose it is state: one integer per key rather than three.

The arithmetic is exact integers in units scaled by the rate. Written the
textbook way GCRA needs an emission interval ``1000 / rate_per_sec``
milliseconds, which is not whole for most rates; multiplying through by the rate
clears it, so one permit costs exactly 1,000 units and one millisecond is
``rate_per_sec`` of them.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

DEFAULT_RATE_PER_SEC = 100
DEFAULT_BURST = 100

UNIT = 1000
"""Scaled units in one permit. One millisecond is ``rate_per_sec`` of them."""


class Gcra:
    """A theoretical arrival time per key."""

    __slots__ = ("_burst", "_rate_per_sec", "_tat", "_tolerance")

    def __init__(
        self, rate_per_sec: int = DEFAULT_RATE_PER_SEC, burst: int = DEFAULT_BURST
    ) -> None:
        if (
            not isinstance(rate_per_sec, int)
            or isinstance(rate_per_sec, bool)
            or rate_per_sec < 1
        ):
            msg = f"Gcra: rate_per_sec must be a positive integer, received {rate_per_sec!r}"
            raise ValueError(msg)
        if not isinstance(burst, int) or isinstance(burst, bool) or burst < 1:
            msg = f"Gcra: burst must be a positive integer, received {burst!r}"
            raise ValueError(msg)

        self._rate_per_sec = rate_per_sec
        self._burst = burst
        # The burst tolerance, in scaled units: how far ahead of now a TAT may
        # sit and still be conforming.
        self._tolerance = (burst - 1) * UNIT
        self._tat: dict[int, int] = {}

    def allow(self, key: int, cost: int, now: int) -> bool:
        """May a request of ``cost`` permits for ``key`` proceed at ``now``?"""
        # A cost above the burst can never be met. The conformance test below
        # does not cap on its own — an idle key's TAT sits arbitrarily far in
        # the past — so the ceiling has to be stated.
        if cost > self._burst:
            return False

        scaled = now * self._rate_per_sec
        tat = self._tat.get(key, scaled)

        if scaled < tat - self._tolerance + (cost - 1) * UNIT:
            return False

        # ``max`` is what stops an idle key banking unbounded credit: the
        # schedule restarts from now rather than from a TAT left in the past.
        self._tat[key] = max(scaled, tat) + cost * UNIT
        return True

    def retry_after(self, key: int, now: int) -> int:
        """Milliseconds until one more permit would be admitted.

        Exact. The TAT already says when the next request is due, so the answer
        is a subtraction and a ceiling division rather than a search.
        """
        tat = self._tat.get(key)
        if tat is None:
            return 0

        scaled = now * self._rate_per_sec
        target = tat - self._tolerance
        if scaled >= target:
            return 0

        # The first whole millisecond at or past the target.
        return -(-target // self._rate_per_sec) - now

    def state_size(self) -> int:
        """How many keys are tracked. One integer each — the reason to use this."""
        return len(self._tat)

    def tokens_of(self, key: int, now: int) -> int:
        """Whole permits available, derived from the TAT rather than stored.

        Spelled the same as the token bucket's ``tokens_of`` so the two
        policies' vectors can be read side by side. Not part of the interface;
        used by tests.
        """
        tat = self._tat.get(key)
        if tat is None:
            return self._burst

        available = now * self._rate_per_sec - (tat - self._burst * UNIT)
        if available <= 0:
            return 0
        return min(self._burst, available // UNIT)

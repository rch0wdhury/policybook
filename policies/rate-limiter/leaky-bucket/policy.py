"""LeakyBucket — a level that rises with each request and drains at a steady rate.

The meter formulation: every admitted request adds one unit to a bucket that
leaks continuously at ``rate_per_sec``, and a request is refused when it would
overflow ``capacity``. At the default capacity of 1 it enforces even spacing —
one request every ``1000 / rate_per_sec`` milliseconds, never two together.

At equal parameters this is the token bucket exactly, under the substitution
``tokens = capacity - level``. A vector in each policy pins the equivalence.

The ledger is integer arithmetic: the level is
whole and the fraction of a unit lives in a ``credit`` accumulator measured in
thousandths.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

DEFAULT_RATE_PER_SEC = 100
DEFAULT_CAPACITY = 1


class LeakyBucket:
    """A draining level per key."""

    __slots__ = ("_capacity", "_credit", "_drain_ms", "_last", "_level", "_rate_per_sec")

    def __init__(
        self, rate_per_sec: int = DEFAULT_RATE_PER_SEC, capacity: int = DEFAULT_CAPACITY
    ) -> None:
        if (
            not isinstance(rate_per_sec, int)
            or isinstance(rate_per_sec, bool)
            or rate_per_sec < 1
        ):
            msg = f"LeakyBucket: rate_per_sec must be a positive integer, received {rate_per_sec!r}"
            raise ValueError(msg)
        if not isinstance(capacity, int) or isinstance(capacity, bool) or capacity < 1:
            msg = f"LeakyBucket: capacity must be a positive integer, received {capacity!r}"
            raise ValueError(msg)

        self._rate_per_sec = rate_per_sec
        self._capacity = capacity
        # How long a completely full bucket takes to drain. Idle time is clamped
        # to this before the multiply, so a key untouched for a month cannot
        # overflow the arithmetic in the C port.
        self._drain_ms = -(-capacity * 1000 // rate_per_sec)

        self._level: dict[int, int] = {}
        self._credit: dict[int, int] = {}
        self._last: dict[int, int] = {}

    def _drain(self, key: int, now: int) -> None:
        """Let a bucket leak up to ``now``."""
        elapsed = now - self._last[key]
        if elapsed <= 0:
            return
        elapsed = min(elapsed, self._drain_ms)

        credit = self._credit[key] + self._rate_per_sec * elapsed
        drained = credit // 1000
        credit %= 1000

        if drained >= self._level[key]:
            # The bucket has run dry. Nothing further leaks, and the fraction
            # that would have leaked next is discarded.
            self._level[key] = 0
            credit = 0
        else:
            self._level[key] -= drained

        self._credit[key] = credit
        self._last[key] = now

    def _bucket_for(self, key: int, now: int) -> None:
        if key not in self._level:
            # A key never seen starts empty: it has been draining for all of
            # history.
            self._level[key] = 0
            self._credit[key] = 0
            self._last[key] = now
            return
        self._drain(key, now)

    def allow(self, key: int, cost: int, now: int) -> bool:
        """May a request of ``cost`` units for ``key`` proceed at ``now``?"""
        self._bucket_for(key, now)
        if self._level[key] + cost > self._capacity:
            return False
        self._level[key] += cost
        return True

    def retry_after(self, key: int, now: int) -> int:
        """Milliseconds until one unit of room exists."""
        if key not in self._level:
            return 0

        self._drain(key, now)
        if self._level[key] < self._capacity:
            return 0

        deficit = 1000 - self._credit[key]
        return -(-deficit // self._rate_per_sec)

    def state_size(self) -> int:
        """How many keys are tracked. Three integers each."""
        return len(self._level)

    def level_of(self, key: int, now: int) -> int:
        """The current level. Not part of the interface; used by tests."""
        if key not in self._level:
            return 0
        self._drain(key, now)
        return self._level[key]

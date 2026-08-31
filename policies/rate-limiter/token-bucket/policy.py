"""TokenBucket — spend from a balance that refills at a steady rate.

The default rate limiter. A key holds up to ``burst`` tokens; each request
spends one; the balance refills at ``rate_per_sec`` and stops at ``burst``. That
gives a long-run ceiling of ``rate_per_sec`` and lets a caller who has been quiet
spend what it saved.

Against the window policies its advantage is that it has no window, so a refused
caller learns exactly how many milliseconds to wait rather than "up to a whole
window".

The ledger is integer arithmetic. Tokens are
whole; the fraction lives in a ``credit`` accumulator measured in thousandths of
a token. A float balance would drift differently in three languages.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

DEFAULT_RATE_PER_SEC = 100
DEFAULT_BURST = 100


class TokenBucket:
    """A refilling token balance per key."""

    __slots__ = ("_burst", "_credit", "_fill_ms", "_last", "_rate_per_sec", "_tokens")

    def __init__(
        self, rate_per_sec: int = DEFAULT_RATE_PER_SEC, burst: int = DEFAULT_BURST
    ) -> None:
        if (
            not isinstance(rate_per_sec, int)
            or isinstance(rate_per_sec, bool)
            or rate_per_sec < 1
        ):
            msg = f"TokenBucket: rate_per_sec must be a positive integer, received {rate_per_sec!r}"
            raise ValueError(msg)
        if not isinstance(burst, int) or isinstance(burst, bool) or burst < 1:
            msg = f"TokenBucket: burst must be a positive integer, received {burst!r}"
            raise ValueError(msg)

        self._rate_per_sec = rate_per_sec
        self._burst = burst
        # How long a completely empty bucket takes to fill. Idle time is clamped
        # to this before the multiply, so a key untouched for a month cannot
        # overflow the arithmetic in the C port. The result is identical either
        # way, because the bucket saturates long before.
        self._fill_ms = -(-burst * 1000 // rate_per_sec)

        self._tokens: dict[int, int] = {}
        self._credit: dict[int, int] = {}
        self._last: dict[int, int] = {}

    def _refill(self, key: int, now: int) -> None:
        """Bring a bucket up to date at ``now``."""
        elapsed = now - self._last[key]
        if elapsed <= 0:
            return
        elapsed = min(elapsed, self._fill_ms)

        credit = self._credit[key] + self._rate_per_sec * elapsed
        tokens = self._tokens[key] + credit // 1000
        credit %= 1000

        if tokens >= self._burst:
            # The bucket overflows: tokens above ``burst`` are lost, and so is
            # the fraction that would have become the next one.
            tokens = self._burst
            credit = 0

        self._tokens[key] = tokens
        self._credit[key] = credit
        self._last[key] = now

    def _bucket_for(self, key: int, now: int) -> None:
        if key not in self._tokens:
            # A key never seen starts full. It has been idle for all of history,
            # and a bucket that started empty would refuse a first request for
            # no reason a caller could act on.
            self._tokens[key] = self._burst
            self._credit[key] = 0
            self._last[key] = now
            return
        self._refill(key, now)

    def allow(self, key: int, cost: int, now: int) -> bool:
        """May a request of ``cost`` tokens for ``key`` proceed at ``now``?"""
        self._bucket_for(key, now)
        if self._tokens[key] < cost:
            return False
        self._tokens[key] -= cost
        return True

    def retry_after(self, key: int, now: int) -> int:
        """Milliseconds until one more token exists.

        Exact, and the reason a token bucket is pleasant to build on: the answer
        comes from the ledger rather than from a window edge.
        """
        if key not in self._tokens:
            return 0

        self._refill(key, now)
        if self._tokens[key] >= 1:
            return 0

        # Ceiling division: the token arrives at the first whole millisecond
        # where the credit reaches 1,000.
        deficit = 1000 - self._credit[key]
        return -(-deficit // self._rate_per_sec)

    def state_size(self) -> int:
        """How many keys are tracked. Three integers each."""
        return len(self._tokens)

    def tokens_of(self, key: int, now: int) -> int:
        """Whole tokens available. Not part of the interface; used by tests."""
        if key not in self._tokens:
            return self._burst
        self._refill(key, now)
        return self._tokens[key]

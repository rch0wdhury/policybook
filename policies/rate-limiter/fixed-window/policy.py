"""FixedWindow — count requests inside a clock-aligned window, reset at the edge.

The simplest limiter that works, and the one most services start with. Two
integers per key, and a counter that a Redis ``INCR`` with a TTL implements
exactly.

Its one flaw is not subtle: a client can send ``limit`` requests at the end of
one window and ``limit`` more at the start of the next, putting ``2 x limit``
through in an interval shorter than a single window.

Windows are aligned to the epoch rather than to a key's first request, which is
what lets separate processes agree on the current window without coordinating.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

DEFAULT_LIMIT = 100
DEFAULT_WINDOW_MS = 1000


class FixedWindow:
    """A counter per key, reset at every window edge."""

    __slots__ = ("_counts", "_limit", "_starts", "_window_ms")

    def __init__(self, limit: int = DEFAULT_LIMIT, window_ms: int = DEFAULT_WINDOW_MS) -> None:
        if not isinstance(limit, int) or isinstance(limit, bool) or limit < 1:
            msg = f"FixedWindow: limit must be a positive integer, received {limit!r}"
            raise ValueError(msg)
        if not isinstance(window_ms, int) or isinstance(window_ms, bool) or window_ms < 1:
            msg = f"FixedWindow: window_ms must be a positive integer, received {window_ms!r}"
            raise ValueError(msg)

        self._limit = limit
        self._window_ms = window_ms
        # Two parallel dicts rather than one dict of pairs: the same two
        # integers per key, without a tuple allocation on every window roll.
        self._starts: dict[int, int] = {}
        self._counts: dict[int, int] = {}

    def _window_of(self, now: int) -> int:
        """The start of the window containing ``now``. Integer division."""
        return now - (now % self._window_ms)

    def allow(self, key: int, cost: int, now: int) -> bool:
        """May a request of ``cost`` units for ``key`` proceed at ``now``?"""
        window_start = self._window_of(now)

        if self._starts.get(key) != window_start:
            # A new window, or a key never seen: the old count is gone, however
            # recently it was earned. This discontinuity is the whole trade-off.
            self._starts[key] = window_start
            self._counts[key] = 0

        count = self._counts[key]
        if count + cost > self._limit:
            return False
        self._counts[key] = count + cost
        return True

    def retry_after(self, key: int, now: int) -> int:
        """Milliseconds until one more request would be admitted.

        Exact rather than a guess: the counter resets at the window edge and
        nothing before then can change the answer.
        """
        window_start = self._window_of(now)
        if self._starts.get(key) != window_start:
            return 0
        if self._counts[key] < self._limit:
            return 0
        return window_start + self._window_ms - now

    def state_size(self) -> int:
        """How many keys are tracked.

        This never falls on its own: a key seen once is remembered forever. A
        real deployment gives the counter a TTL of one window, which is what the
        Redis idiom does; keeping the state here puts the memory cost into the
        benchmark rather than hiding it behind a background sweep.
        """
        return len(self._starts)

    def count_of(self, key: int, now: int) -> int:
        """The current count for a key. Not part of the interface; used by tests."""
        if self._starts.get(key) != self._window_of(now):
            return 0
        return self._counts[key]

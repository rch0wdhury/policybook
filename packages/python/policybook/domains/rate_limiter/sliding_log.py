# GENERATED COPY — do not edit. Edit policies/rate-limiter/sliding-log/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""SlidingLog — remember every request time, and count the recent ones.

The only limiter in this domain that enforces its limit exactly: over any window
of ``window_ms`` ending at any instant, the number of admitted requests is at
most ``limit``. No boundary effect, no estimate, no burst that slips through.

That exactness is bought with memory: the log holds ``limit`` timestamps per
key. Expiry is O(1) amortised, since each timestamp is written once and dropped
once.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

from collections import deque

DEFAULT_LIMIT = 100
DEFAULT_WINDOW_MS = 1000


class SlidingLog:
    """A log of admitted timestamps per key, trimmed to the trailing window."""

    __slots__ = ("_limit", "_logs", "_window_ms")

    def __init__(self, limit: int = DEFAULT_LIMIT, window_ms: int = DEFAULT_WINDOW_MS) -> None:
        if not isinstance(limit, int) or isinstance(limit, bool) or limit < 1:
            msg = f"SlidingLog: limit must be a positive integer, received {limit!r}"
            raise ValueError(msg)
        if not isinstance(window_ms, int) or isinstance(window_ms, bool) or window_ms < 1:
            msg = f"SlidingLog: window_ms must be a positive integer, received {window_ms!r}"
            raise ValueError(msg)

        self._limit = limit
        self._window_ms = window_ms
        # A deque bounded to `limit` is the idiomatic Python spelling of the ring
        # buffer the TypeScript and C ports use explicitly. It is not given
        # maxlen, because silently discarding the oldest entry on overflow would
        # hide exactly the bug the limit exists to prevent.
        self._logs: dict[int, deque[int]] = {}

    def _expire(self, log: deque[int], now: int) -> None:
        """Drop every timestamp that has left the window.

        The window is ``(now - window_ms, now]``: a request exactly ``window_ms``
        old has left it. That is what makes "at most ``limit`` in any
        ``window_ms`` interval" true as stated rather than off by one at the edge.
        """
        cutoff = now - self._window_ms
        while log and log[0] <= cutoff:
            log.popleft()

    def allow(self, key: int, cost: int, now: int) -> bool:
        """May a request of ``cost`` units for ``key`` proceed at ``now``?"""
        log = self._logs.get(key)
        if log is None:
            log = deque()
            self._logs[key] = log

        self._expire(log, now)
        if len(log) + cost > self._limit:
            return False

        # A request costing n occupies n slots: it is n requests as far as the
        # limit is concerned, and they all age out together.
        for _ in range(cost):
            log.append(now)
        return True

    def retry_after(self, key: int, now: int) -> int:
        """Milliseconds until one more request would be admitted.

        Exact for a cost of one: the log is full, so admission waits on the
        oldest entry leaving the window, which happens at ``time + window_ms``
        because the window excludes its far end.
        """
        log = self._logs.get(key)
        if log is None:
            return 0

        self._expire(log, now)
        if len(log) < self._limit:
            return 0
        return log[0] + self._window_ms - now

    def state_size(self) -> int:
        """How many keys are tracked. Each costs up to ``limit`` timestamps."""
        return len(self._logs)

    def count_of(self, key: int, now: int) -> int:
        """Live entries in a key's window. Not part of the interface; used by tests."""
        log = self._logs.get(key)
        if log is None:
            return 0
        self._expire(log, now)
        return len(log)

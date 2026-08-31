# GENERATED COPY — do not edit. Edit policies/rate-limiter/sliding-counter/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""SlidingCounter — two fixed windows, weighted by how far into the new one you are.

The practical compromise between a fixed window and a sliding log, and what most
production limiters actually run. Keep the count for the current window and the
previous one, then estimate the rate over the trailing window by fading the old
count out as the new window fills::

    estimate = previous * (window_ms - elapsed) // window_ms + current

Three integers per key instead of a hundred timestamps, and the boundary burst
is gone.

The weighting is integer arithmetic with the remainder discarded. Flooring makes
the estimate err very slightly low, and it makes the three language ports produce
identical decisions rather than diverging on the last bit of a float.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

DEFAULT_LIMIT = 100
DEFAULT_WINDOW_MS = 1000


class SlidingCounter:
    """Two window counts per key, weighted into a single estimate."""

    __slots__ = ("_current", "_limit", "_previous", "_starts", "_window_ms")

    def __init__(self, limit: int = DEFAULT_LIMIT, window_ms: int = DEFAULT_WINDOW_MS) -> None:
        if not isinstance(limit, int) or isinstance(limit, bool) or limit < 1:
            msg = f"SlidingCounter: limit must be a positive integer, received {limit!r}"
            raise ValueError(msg)
        if not isinstance(window_ms, int) or isinstance(window_ms, bool) or window_ms < 1:
            msg = f"SlidingCounter: window_ms must be a positive integer, received {window_ms!r}"
            raise ValueError(msg)

        self._limit = limit
        self._window_ms = window_ms
        self._starts: dict[int, int] = {}
        self._current: dict[int, int] = {}
        self._previous: dict[int, int] = {}

    def _window_of(self, now: int) -> int:
        """The start of the window containing ``now``. Integer division."""
        return now - (now % self._window_ms)

    def _advance(self, key: int, window_start: int) -> None:
        """Roll a key's counters forward to ``window_start``."""
        previous_start = self._starts.get(key)
        if previous_start == window_start:
            return

        if previous_start is None:
            self._previous[key] = 0
        elif window_start == previous_start + self._window_ms:
            # The very next window: today's count becomes yesterday's.
            self._previous[key] = self._current[key]
        else:
            # A gap of two windows or more — nothing from before is still in the
            # trailing window, so both counts go.
            self._previous[key] = 0

        self._current[key] = 0
        self._starts[key] = window_start

    def _estimate(self, key: int, now: int) -> int:
        """``previous * (window_ms - elapsed) // window_ms + current``, floored."""
        elapsed = now - self._starts[key]
        carried = self._previous[key] * (self._window_ms - elapsed) // self._window_ms
        return carried + self._current[key]

    def allow(self, key: int, cost: int, now: int) -> bool:
        """May a request of ``cost`` units for ``key`` proceed at ``now``?"""
        self._advance(key, self._window_of(now))

        if self._estimate(key, now) + cost > self._limit:
            return False
        self._current[key] += cost
        return True

    def retry_after(self, key: int, now: int) -> int:
        """Milliseconds until one more request would be admitted.

        Solved rather than searched. Admission needs the carried part to fall to
        ``limit - current - 1`` or below, and since it decays linearly::

            carried <= need  <=>  previous * (window_ms - elapsed) < (need + 1) * window_ms
                             <=>  elapsed > window_ms * (previous - need - 1) / previous
        """
        if key not in self._starts:
            return 0

        window_start = self._window_of(now)
        self._advance(key, window_start)
        if self._estimate(key, now) < self._limit:
            return 0

        current = self._current[key]
        previous = self._previous[key]

        need = self._limit - current - 1
        if need < 0:
            # The current window has reached the limit on its own, so the wait
            # runs past the edge — but not only to the edge. At the edge this
            # count becomes the previous count and, undecayed, still refuses.
            # ``allow`` never lets ``current`` exceed ``limit``, so one further
            # millisecond of decay is always enough.
            return window_start + self._window_ms - now + 1
        if previous == 0:
            return 0

        # ``excess == 0`` means the carried count is exactly one too high, which
        # still needs a millisecond of decay, so only a negative excess admits
        # immediately.
        excess = previous - need - 1
        if excess < 0:
            return 0

        target = self._window_ms * excess // previous + 1
        elapsed = now - window_start
        return target - elapsed if target > elapsed else 0

    def state_size(self) -> int:
        """How many keys are tracked. Three integers each."""
        return len(self._starts)

    def estimate_of(self, key: int, now: int) -> int:
        """The weighted estimate for a key. Not part of the interface; used by tests."""
        if key not in self._starts:
            return 0
        self._advance(key, self._window_of(now))
        return self._estimate(key, now)

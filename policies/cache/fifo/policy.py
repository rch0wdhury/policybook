"""FIFO — evict the key that arrived first.

The baseline every other cache policy is measured against. It ignores hits
entirely: a key's position is fixed the moment it enters, so however often it is
used, it leaves in arrival order.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

from collections import deque
from typing import Any, Generic, TypeVar

K = TypeVar("K")

DEFAULT_CAPACITY = 1000


class Fifo(Generic[K]):
    """Evict in arrival order."""

    __slots__ = ("_capacity", "_queue")

    def __init__(self, capacity: int = DEFAULT_CAPACITY) -> None:
        if not isinstance(capacity, int) or isinstance(capacity, bool) or capacity < 1:
            msg = f"Fifo: capacity must be a positive integer, received {capacity!r}"
            raise ValueError(msg)
        self._capacity = capacity
        self._queue: deque[K] = deque()

    def on_access(self, key: K, hit: bool, meta: dict[str, Any] | None = None) -> None:
        """Record an access. A hit changes nothing — FIFO does not learn."""
        del meta
        if hit:
            return

        if len(self._queue) > self._capacity:
            msg = (
                f"Fifo: {len(self._queue)} entries inserted without an evict, "
                f"capacity is {self._capacity}. Call evict() once the cache is over capacity."
            )
            raise RuntimeError(msg)

        self._queue.append(key)

    def evict(self) -> K:
        """Remove and return the key that arrived first."""
        if not self._queue:
            msg = "Fifo: evict() called with nothing resident"
            raise RuntimeError(msg)
        return self._queue.popleft()

    def size(self) -> int:
        """Entries currently held. Not part of the interface; used by tests."""
        return len(self._queue)

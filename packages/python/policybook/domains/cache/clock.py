# GENERATED COPY — do not edit. Edit policies/cache/clock/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""CLOCK — approximate LRU with one reference bit per entry.

A hit sets a bit and does nothing else, so reads need no lock. Eviction walks a
hand around the entries in arrival order, clearing bits and sparing whatever it
finds set.

Mirrors ``index.ts``: the queue formulation, which is the same algorithm as the
circular buffer with a rotating hand. The queue's front *is* the hand.
"""

from __future__ import annotations

from collections import deque
from typing import Any, Generic, TypeVar

K = TypeVar("K")

DEFAULT_CAPACITY = 1000


class Clock(Generic[K]):
    """Evict the first entry the hand finds without its reference bit set."""

    __slots__ = ("_capacity", "_order", "_referenced")

    def __init__(self, capacity: int = DEFAULT_CAPACITY) -> None:
        if not isinstance(capacity, int) or isinstance(capacity, bool) or capacity < 1:
            msg = f"Clock: capacity must be a positive integer, received {capacity!r}"
            raise ValueError(msg)
        self._capacity = capacity
        # Arrival order; the front is the hand.
        self._order: deque[K] = deque()
        self._referenced: dict[K, bool] = {}

    def on_access(self, key: K, hit: bool, meta: dict[str, Any] | None = None) -> None:
        """Record an access. A hit sets one bit and reorders nothing."""
        del meta
        if hit:
            if key not in self._referenced:
                msg = f"Clock: on_access reported a hit for a key it does not hold: {key!r}"
                raise RuntimeError(msg)
            self._referenced[key] = True
            return

        if len(self._order) > self._capacity:
            msg = (
                f"Clock: {len(self._order)} entries inserted without an evict, "
                f"capacity is {self._capacity}. Call evict() once the cache is over capacity."
            )
            raise RuntimeError(msg)

        self._order.append(key)
        self._referenced[key] = False

    def evict(self) -> K:
        """Remove and return the first entry without a second chance."""
        if not self._order:
            msg = "Clock: evict() called with nothing resident"
            raise RuntimeError(msg)

        # The hand can pass each entry at most once, because passing it clears
        # its bit.
        while True:
            key = self._order.popleft()
            if self._referenced[key]:
                self._referenced[key] = False
                self._order.append(key)
                continue
            del self._referenced[key]
            return key

    def size(self) -> int:
        """Entries currently held. Not part of the interface; used by tests."""
        return len(self._order)

    def is_referenced(self, key: K) -> bool:
        """Whether a key's reference bit is set. Used by tests."""
        return self._referenced.get(key, False)

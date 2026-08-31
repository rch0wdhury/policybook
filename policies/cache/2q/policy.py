"""2Q — admit to the main cache only on a second access.

New keys audition in A1in, a small FIFO. Keys evicted from it leave their
identifier in A1out, a ghost queue with no values behind it. A key that returns
while its ghost is live has proven reuse and is promoted to Am, the main LRU.

A scan therefore never reaches Am: its keys arrive once, pass through A1in, and
leave.

Mirrors ``index.ts``. A hit on a key still in A1in does nothing — that is the
algorithm, not an omission.
"""

from __future__ import annotations

from collections import OrderedDict
from typing import Any, Generic, Literal, TypeVar

K = TypeVar("K")

DEFAULT_CAPACITY = 1000
DEFAULT_KIN = 0.25
DEFAULT_KOUT = 0.5

TwoQueueLocation = Literal["a1in", "am", "ghost", "absent"]


class TwoQueue(Generic[K]):
    """Evict from the admission queue first, and the main LRU only after."""

    __slots__ = ("_admission", "_capacity", "_ghost", "_kin_size", "_kout_size", "_main")

    def __init__(
        self,
        capacity: int = DEFAULT_CAPACITY,
        kin: float = DEFAULT_KIN,
        kout: float = DEFAULT_KOUT,
    ) -> None:
        if not isinstance(capacity, int) or isinstance(capacity, bool) or capacity < 1:
            msg = f"TwoQueue: capacity must be a positive integer, received {capacity!r}"
            raise ValueError(msg)
        if not 0 < kin <= 1:
            msg = f"TwoQueue: kin must be a fraction in (0, 1], received {kin!r}"
            raise ValueError(msg)
        if not 0 < kout <= 1:
            msg = f"TwoQueue: kout must be a fraction in (0, 1], received {kout!r}"
            raise ValueError(msg)

        self._capacity = capacity
        # Both fractions floor to at least one entry, so a small cache still has
        # a working admission queue rather than degenerating into LRU.
        self._kin_size = max(1, int(capacity * kin))
        self._kout_size = max(1, int(capacity * kout))

        # Arrival order, oldest first. An OrderedDict rather than a deque so
        # the hit path's membership test is O(1); a resident key is never
        # appended twice, so insertion order is exactly arrival order.
        self._admission: OrderedDict[K, None] = OrderedDict()
        # Most recently used last, so the eviction candidate is the first item.
        self._main: OrderedDict[K, None] = OrderedDict()
        self._ghost: OrderedDict[K, None] = OrderedDict()

    def on_access(self, key: K, hit: bool, meta: dict[str, Any] | None = None) -> None:
        """Record an access, promoting only on a return from the ghost queue."""
        del meta
        if hit:
            # A hit in Am refreshes recency. A hit in A1in does nothing at all:
            # the key has not yet earned promotion, and reordering A1in would
            # make it a second LRU rather than an audition.
            if key in self._main:
                self._main.move_to_end(key)
            elif key not in self._admission:
                msg = f"TwoQueue: on_access reported a hit for a key it does not hold: {key!r}"
                raise RuntimeError(msg)
            return

        if len(self._admission) + len(self._main) > self._capacity:
            msg = (
                f"TwoQueue: {len(self._admission) + len(self._main)} entries inserted without "
                f"an evict, capacity is {self._capacity}. "
                "Call evict() once the cache is over capacity."
            )
            raise RuntimeError(msg)

        if key in self._ghost:
            # Seen before and back again: this is the second access the policy
            # is waiting for, so the key goes straight into the main cache.
            del self._ghost[key]
            self._main[key] = None
            return

        self._admission[key] = None

    def evict(self) -> K:
        """Remove the admission queue's oldest entry, or the main LRU's."""
        # Drain A1in while it is over its share: an entry that has not proven
        # reuse is always a better victim than one that has.
        if self._admission and (len(self._admission) > self._kin_size or not self._main):
            key, _ = self._admission.popitem(last=False)
            # Remember the key so a prompt return promotes it. Only A1in
            # evictions leave a ghost; an Am entry already proved itself once.
            self._remember_ghost(key)
            return key

        if not self._main:
            msg = "TwoQueue: evict() called with nothing resident"
            raise RuntimeError(msg)

        key, _ = self._main.popitem(last=False)
        return key

    def size(self) -> int:
        """Entries currently held. Not part of the interface; used by tests."""
        return len(self._admission) + len(self._main)

    def queue_of(self, key: K) -> TwoQueueLocation:
        """Where a key lives right now. Not part of the interface; used by tests."""
        if key in self._main:
            return "am"
        if key in self._admission:
            return "a1in"
        if key in self._ghost:
            return "ghost"
        return "absent"

    def _remember_ghost(self, key: K) -> None:
        if len(self._ghost) >= self._kout_size:
            # A key whose ghost has expired has to start from A1in again.
            self._ghost.popitem(last=False)
        self._ghost[key] = None

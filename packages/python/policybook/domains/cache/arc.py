# GENERATED COPY — do not edit. Edit policies/cache/arc/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""ARC — balance recency against frequency, and tune the balance itself.

Four lists: T1 for keys seen once recently, T2 for keys seen at least twice, and
ghost lists B1 and B2 holding the identifiers of keys evicted from each. A target
``p`` says how much of the cache T1 should get, and every ghost hit moves it: a
hit in B1 means recency was undervalued, a hit in B2 means frequency was.

Mirrors ``index.ts``, implemented from the paper's Figure 4. See ``README.md``
for the patent history before commercial use.
"""

from __future__ import annotations

from collections import OrderedDict
from typing import Any, Generic, Literal, TypeVar

K = TypeVar("K")

DEFAULT_CAPACITY = 1000

ArcList = Literal["t1", "t2", "b1", "b2", "absent"]


class Arc(Generic[K]):
    """Adaptive replacement: T1 and T2 for entries, B1 and B2 for their ghosts."""

    __slots__ = ("_b1", "_b2", "_capacity", "_pending_victim", "_t1", "_t2", "_target")

    def __init__(self, capacity: int = DEFAULT_CAPACITY) -> None:
        if not isinstance(capacity, int) or isinstance(capacity, bool) or capacity < 1:
            msg = f"Arc: capacity must be a positive integer, received {capacity!r}"
            raise ValueError(msg)
        self._capacity = capacity
        # Most recently used last in every list, so the oldest is the first item.
        self._t1: OrderedDict[K, None] = OrderedDict()
        self._t2: OrderedDict[K, None] = OrderedDict()
        self._b1: OrderedDict[K, None] = OrderedDict()
        self._b2: OrderedDict[K, None] = OrderedDict()
        self._target = 0
        # ARC decides what to replace while handling the miss, because the
        # decision depends on list sizes before the new key is inserted. The
        # interface asks for the victim afterwards, so it waits here.
        self._pending_victim: K | None = None

    def on_access(self, key: K, hit: bool, meta: dict[str, Any] | None = None) -> None:
        """Record an access, adapting the target on a ghost hit."""
        del meta

        if hit:
            if key in self._t1:
                del self._t1[key]
            elif key in self._t2:
                del self._t2[key]
            else:
                msg = f"Arc: on_access reported a hit for a key it does not hold: {key!r}"
                raise RuntimeError(msg)
            # Case I: a second recent use promotes the key to the frequent list.
            self._t2[key] = None
            return

        if key in self._t1 or key in self._t2:
            msg = f"Arc: on_access reported a miss for a resident key: {key!r}"
            raise RuntimeError(msg)

        b1_len = len(self._b1)
        b2_len = len(self._b2)

        if key in self._b1:
            # Case II: recency was undervalued, so give T1 more room.
            delta = 1 if b1_len >= b2_len else b2_len // b1_len
            self._target = min(self._capacity, self._target + delta)
            self._replace(returning_from_b2=False)
            del self._b1[key]
            self._t2[key] = None
            return

        if key in self._b2:
            # Case III: frequency was undervalued, so take room away from T1.
            delta = 1 if b2_len >= b1_len else b1_len // b2_len
            self._target = max(0, self._target - delta)
            self._replace(returning_from_b2=True)
            del self._b2[key]
            self._t2[key] = None
            return

        # Case IV: a key ARC has no memory of at all.
        t1_len = len(self._t1)
        total = t1_len + len(self._t2) + b1_len + b2_len

        if t1_len + b1_len == self._capacity:
            if t1_len < self._capacity:
                self._b1.popitem(last=False)
                self._replace(returning_from_b2=False)
            else:
                # T1 is the whole cache and there are no recent ghosts, so the
                # oldest recent entry goes without leaving one: the invariant
                # |T1| + |B1| <= c leaves nowhere to put it.
                victim, _ = self._t1.popitem(last=False)
                self._pending_victim = victim
        elif t1_len + b1_len < self._capacity and total >= self._capacity:
            if total == 2 * self._capacity:
                self._b2.popitem(last=False)
            self._replace(returning_from_b2=False)

        self._t1[key] = None

    def evict(self) -> K:
        """Return the key ARC chose while handling the miss."""
        if self._pending_victim is None:
            msg = (
                "Arc: evict() called when no replacement was scheduled. ARC chooses its "
                "victim while handling the miss, so evict() is only valid once the cache "
                "is over capacity."
            )
            raise RuntimeError(msg)
        key = self._pending_victim
        self._pending_victim = None
        return key

    def size(self) -> int:
        """Entries currently held, including one already chosen for eviction."""
        return len(self._t1) + len(self._t2) + (0 if self._pending_victim is None else 1)

    def target_t1(self) -> int:
        """The current target size for T1. Not part of the interface; used by tests."""
        return self._target

    def list_of_key(self, key: K) -> ArcList:
        """Which list a key is in. Not part of the interface; used by tests."""
        if key in self._t1:
            return "t1"
        if key in self._t2:
            return "t2"
        if key in self._b1:
            return "b1"
        if key in self._b2:
            return "b2"
        return "absent"

    def _replace(self, *, returning_from_b2: bool) -> None:
        """The paper's REPLACE: demote the oldest of T1 or T2 to its ghost list.

        T1 gives up an entry when it is over its target — or when it is exactly
        at target and the key that caused this came back from B2, which is a
        hint that the frequent side deserves the benefit of the doubt.
        """
        t1_len = len(self._t1)
        take_from_t1 = t1_len >= 1 and (
            (returning_from_b2 and t1_len == self._target) or t1_len > self._target
        )

        if take_from_t1:
            victim, _ = self._t1.popitem(last=False)
            self._b1[victim] = None
        else:
            if not self._t2:
                msg = "Arc: evict() called with nothing resident"
                raise RuntimeError(msg)
            victim, _ = self._t2.popitem(last=False)
            self._b2[victim] = None

        self._pending_victim = victim

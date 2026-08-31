# GENERATED COPY — do not edit. Edit policies/cache/lru/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""LRU — evict the key used longest ago.

The default baseline. Every hit moves a key to the front of a recency order;
eviction takes from the back. The bet is that recency predicts reuse.

Mirrors ``index.ts``. Where the TypeScript version keeps its recency list in two
``Int32Array``s, Python gets the same behaviour from ``OrderedDict``, which is
insertion-ordered with O(1) ``move_to_end`` — the identical algorithm, spelled
the way Python spells it.
"""

from __future__ import annotations

from collections import OrderedDict
from typing import Any, Generic, TypeVar

K = TypeVar("K")

DEFAULT_CAPACITY = 1000


class Lru(Generic[K]):
    """Evict the least recently used key."""

    __slots__ = ("_capacity", "_order")

    def __init__(self, capacity: int = DEFAULT_CAPACITY) -> None:
        if not isinstance(capacity, int) or isinstance(capacity, bool) or capacity < 1:
            msg = f"Lru: capacity must be a positive integer, received {capacity!r}"
            raise ValueError(msg)
        self._capacity = capacity
        # Most recently used last, so the eviction candidate is the first item.
        self._order: OrderedDict[K, None] = OrderedDict()

    def on_access(self, key: K, hit: bool, meta: dict[str, Any] | None = None) -> None:
        """Record an access, moving the key to the most-recent end."""
        del meta
        if hit:
            if key not in self._order:
                msg = f"Lru: on_access reported a hit for a key it does not hold: {key!r}"
                raise RuntimeError(msg)
            self._order.move_to_end(key)
            return

        if len(self._order) > self._capacity:
            msg = (
                f"Lru: {len(self._order)} entries inserted without an evict, "
                f"capacity is {self._capacity}. Call evict() once the cache is over capacity."
            )
            raise RuntimeError(msg)

        self._order[key] = None

    def evict(self) -> K:
        """Remove and return the key used longest ago."""
        if not self._order:
            msg = "Lru: evict() called with nothing resident"
            raise RuntimeError(msg)
        key, _ = self._order.popitem(last=False)
        return key

    def size(self) -> int:
        """Entries currently held. Not part of the interface; used by tests."""
        return len(self._order)

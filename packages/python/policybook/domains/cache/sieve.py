# GENERATED COPY — do not edit. Edit policies/cache/sieve/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""SIEVE — a FIFO queue with a hand that gives each entry one chance.

The same shape as CLOCK, differing in one respect that changes its behaviour
entirely: a survivor is **not** moved to the back. It stays where it is, and the
hand stays where it stopped. Old entries must keep earning their place; new
entries face the hand before they have travelled the whole queue.

Mirrors ``index.ts``. Entries are held in a doubly linked list because the hand
removes from the middle; the directions are named ``newer`` and ``older``
rather than the paper's ``prev``/``next``, which are ambiguous when the hand
travels one way and insertion happens at the other end.
"""

from __future__ import annotations

from typing import Any, Generic, TypeVar

K = TypeVar("K")

DEFAULT_CAPACITY = 1000


class _Entry(Generic[K]):
    """One cached key, its visited bit, and its neighbours in insertion order."""

    __slots__ = ("key", "newer", "older", "visited")

    def __init__(self, key: K) -> None:
        self.key = key
        self.visited = False
        self.newer: _Entry[K] | None = None
        self.older: _Entry[K] | None = None


class Sieve(Generic[K]):
    """Evict the first unvisited entry the hand reaches, and leave survivors put."""

    __slots__ = ("_capacity", "_entries", "_hand", "_newest", "_oldest")

    def __init__(self, capacity: int = DEFAULT_CAPACITY) -> None:
        if not isinstance(capacity, int) or isinstance(capacity, bool) or capacity < 1:
            msg = f"Sieve: capacity must be a positive integer, received {capacity!r}"
            raise ValueError(msg)
        self._capacity = capacity
        self._entries: dict[K, _Entry[K]] = {}
        self._newest: _Entry[K] | None = None
        self._oldest: _Entry[K] | None = None
        # Where the hand stopped. Retaining this across evictions is the point:
        # restarting at the oldest entry each time would make this CLOCK.
        self._hand: _Entry[K] | None = None

    def on_access(self, key: K, hit: bool, meta: dict[str, Any] | None = None) -> None:
        """Record an access. A hit sets one bit; the entry does not move."""
        del meta
        if hit:
            entry = self._entries.get(key)
            if entry is None:
                msg = f"Sieve: on_access reported a hit for a key it does not hold: {key!r}"
                raise RuntimeError(msg)
            entry.visited = True
            return

        if len(self._entries) > self._capacity:
            msg = (
                f"Sieve: {len(self._entries)} entries inserted without an evict, "
                f"capacity is {self._capacity}. Call evict() once the cache is over capacity."
            )
            raise RuntimeError(msg)

        # New entries go at the new end, ahead of the hand.
        entry = _Entry(key)
        entry.older = self._newest
        if self._newest is not None:
            self._newest.newer = entry
        else:
            self._oldest = entry
        self._newest = entry
        self._entries[key] = entry

    def evict(self) -> K:
        """Remove and return the first entry the hand finds unvisited."""
        if self._oldest is None:
            msg = "Sieve: evict() called with nothing resident"
            raise RuntimeError(msg)

        # Resume where the hand stopped, or start at the oldest entry.
        entry = self._hand if self._hand is not None else self._oldest

        while entry.visited:
            entry.visited = False
            # Past the newest entry, the hand wraps to the oldest.
            entry = entry.newer if entry.newer is not None else self._oldest
            assert entry is not None

        # The hand stops just beyond the victim, and stays there.
        self._hand = entry.newer

        self._unlink(entry)
        del self._entries[entry.key]
        return entry.key

    def size(self) -> int:
        """Entries currently held. Not part of the interface; used by tests."""
        return len(self._entries)

    def is_visited(self, key: K) -> bool:
        """Whether a key's visited bit is set. Used by tests."""
        entry = self._entries.get(key)
        return entry is not None and entry.visited

    def _unlink(self, entry: _Entry[K]) -> None:
        if entry.newer is not None:
            entry.newer.older = entry.older
        else:
            self._newest = entry.older
        if entry.older is not None:
            entry.older.newer = entry.newer
        else:
            self._oldest = entry.newer
        entry.newer = None
        entry.older = None

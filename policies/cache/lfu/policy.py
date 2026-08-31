"""LFU — evict the key used least often.

Where LRU bets on recency, LFU bets on popularity. On stable skewed traffic that
is the better bet; when popularity shifts it is the worse one, because LFU has
no way to forget.

Mirrors ``index.ts``, including the O(1) construction of Shah, Mitra and Matani:
entries are grouped into frequency classes, the classes form an ascending linked
list, and neither promotion nor eviction ever scans. Within a class, entries are
ordered by when they reached that frequency — ``OrderedDict`` gives that for
free — and the earliest is evicted first.
"""

from __future__ import annotations

from collections import OrderedDict
from typing import Any, Generic, TypeVar

K = TypeVar("K")

DEFAULT_CAPACITY = 1000


class _FrequencyClass(Generic[K]):
    """Every entry with the same access count, plus links to its neighbours."""

    __slots__ = ("entries", "frequency", "next", "prev")

    def __init__(self, frequency: int) -> None:
        self.frequency = frequency
        # Insertion order is the order entries reached this frequency, which is
        # exactly the documented tie-break.
        self.entries: OrderedDict[K, None] = OrderedDict()
        self.prev: _FrequencyClass[K] | None = None
        self.next: _FrequencyClass[K] | None = None


class Lfu(Generic[K]):
    """Evict the least frequently used key."""

    __slots__ = ("_capacity", "_class_of", "_head")

    def __init__(self, capacity: int = DEFAULT_CAPACITY) -> None:
        if not isinstance(capacity, int) or isinstance(capacity, bool) or capacity < 1:
            msg = f"Lfu: capacity must be a positive integer, received {capacity!r}"
            raise ValueError(msg)
        self._capacity = capacity
        self._class_of: dict[K, _FrequencyClass[K]] = {}
        # The lowest-frequency class: the eviction candidates.
        self._head: _FrequencyClass[K] | None = None

    def on_access(self, key: K, hit: bool, meta: dict[str, Any] | None = None) -> None:
        """Record an access, promoting the key to the next frequency class."""
        del meta
        if hit:
            if key not in self._class_of:
                msg = f"Lfu: on_access reported a hit for a key it does not hold: {key!r}"
                raise RuntimeError(msg)
            self._promote(key)
            return

        if len(self._class_of) > self._capacity:
            msg = (
                f"Lfu: {len(self._class_of)} entries inserted without an evict, "
                f"capacity is {self._capacity}. Call evict() once the cache is over capacity."
            )
            raise RuntimeError(msg)

        # A new entry has frequency 1, so it belongs at the front of the list.
        head = self._head
        target = head if head is not None and head.frequency == 1 else self._insert_front(1)
        target.entries[key] = None
        self._class_of[key] = target

    def evict(self) -> K:
        """Remove and return the least frequently used key."""
        klass = self._head
        if klass is None:
            msg = "Lfu: evict() called with nothing resident"
            raise RuntimeError(msg)

        # The first class holds the lowest frequency; its first entry reached
        # that frequency earliest.
        key = next(iter(klass.entries))
        del klass.entries[key]
        if not klass.entries:
            self._unlink(klass)
        del self._class_of[key]
        return key

    def size(self) -> int:
        """Entries currently held. Not part of the interface; used by tests."""
        return len(self._class_of)

    def frequency_of(self, key: K) -> int:
        """The access count recorded for a key, or 0. Used by tests."""
        klass = self._class_of.get(key)
        return 0 if klass is None else klass.frequency

    def _promote(self, key: K) -> None:
        current = self._class_of[key]
        following = current.next

        # Reuse the neighbouring class if it is already the frequency we want.
        if following is not None and following.frequency == current.frequency + 1:
            target = following
        else:
            target = self._insert_after(current, current.frequency + 1)

        del current.entries[key]
        if not current.entries:
            self._unlink(current)

        target.entries[key] = None
        self._class_of[key] = target

    def _insert_front(self, frequency: int) -> _FrequencyClass[K]:
        klass: _FrequencyClass[K] = _FrequencyClass(frequency)
        klass.next = self._head
        if self._head is not None:
            self._head.prev = klass
        self._head = klass
        return klass

    def _insert_after(
        self, after: _FrequencyClass[K], frequency: int
    ) -> _FrequencyClass[K]:
        klass: _FrequencyClass[K] = _FrequencyClass(frequency)
        klass.prev = after
        klass.next = after.next
        if after.next is not None:
            after.next.prev = klass
        after.next = klass
        return klass

    def _unlink(self, klass: _FrequencyClass[K]) -> None:
        # An empty class carries no information and must not be left in the
        # list, or eviction would find nothing in it.
        if klass.prev is not None:
            klass.prev.next = klass.next
        else:
            self._head = klass.next
        if klass.next is not None:
            klass.next.prev = klass.prev
        klass.prev = None
        klass.next = None

"""Bélády's OPT — evict whatever will be needed furthest in the future.

Not a deployable policy: it requires the entire future access sequence. It is
here because it is the *bound*. No online policy can beat it on the same trace,
so the gap between a policy's hit rate and OPT's is what is still on the table.

Mirrors ``index.ts``. The heap is Python's ``heapq`` with lazy invalidation
rather than the indexed heap the TypeScript version uses; the eviction sequence
is identical because the ordering — furthest next use, then earliest insertion —
is the same, and the shared vectors check it.
"""

from __future__ import annotations

import heapq
from typing import Any, Generic, TypeVar

K = TypeVar("K")

DEFAULT_CAPACITY = 1000


class Opt(Generic[K]):
    """Evict the resident key whose next use is furthest away."""

    __slots__ = (
        "_capacity",
        "_future",
        "_heap",
        "_insert_counter",
        "_inserted_at",
        "_next_use",
        "_next_use_at",
        "_step",
    )

    def __init__(self, capacity: int = DEFAULT_CAPACITY, future: list[K] | None = None) -> None:
        if not isinstance(capacity, int) or isinstance(capacity, bool) or capacity < 1:
            msg = f"Opt: capacity must be a positive integer, received {capacity!r}"
            raise ValueError(msg)

        sequence: list[K] = [] if future is None else list(future)
        self._capacity = capacity
        self._future = sequence
        self._step = 0

        # One backward pass records where each key is next used. The trace
        # length stands for "never again": it is strictly greater than any real
        # position, so every comparison stays an integer one.
        self._next_use_at: list[int] = [len(sequence)] * len(sequence)
        last_seen: dict[K, int] = {}
        for position in range(len(sequence) - 1, -1, -1):
            key = sequence[position]
            self._next_use_at[position] = last_seen.get(key, len(sequence))
            last_seen[key] = position

        self._next_use: dict[K, int] = {}
        self._inserted_at: dict[K, int] = {}
        self._insert_counter = 0
        # (-next use, insertion order, key): a min-heap ordered so the furthest
        # next use comes out first, ties going to the earliest inserted.
        self._heap: list[tuple[int, int, K]] = []

    def on_access(self, key: K, hit: bool, meta: dict[str, Any] | None = None) -> None:
        """Record an access and update the key's next use."""
        del meta

        if self._step >= len(self._future):
            msg = (
                f"Opt: access beyond the end of the supplied future ({len(self._future)} "
                "events). OPT must be given the whole trace it will be run on."
            )
            raise RuntimeError(msg)
        if self._future[self._step] != key:
            msg = (
                f"Opt: the trace does not match the supplied future at event {self._step}: "
                f"expected {self._future[self._step]!r}, got {key!r}. "
                "OPT's result is only a bound for the trace it was given."
            )
            raise RuntimeError(msg)

        next_use = self._next_use_at[self._step]
        self._step += 1

        if hit:
            if key not in self._next_use:
                msg = f"Opt: on_access reported a hit for a key it does not hold: {key!r}"
                raise RuntimeError(msg)
        else:
            if len(self._next_use) > self._capacity:
                msg = (
                    f"Opt: {len(self._next_use)} entries inserted without an evict, capacity "
                    f"is {self._capacity}. Call evict() once the cache is over capacity."
                )
                raise RuntimeError(msg)
            self._inserted_at[key] = self._insert_counter
            self._insert_counter += 1

        self._next_use[key] = next_use
        heapq.heappush(self._heap, (-next_use, self._inserted_at[key], key))

    def evict(self) -> K:
        """Remove and return the key whose next use is furthest away."""
        while self._heap:
            negated, _, key = heapq.heappop(self._heap)
            # An entry is stale if the key has since been evicted, or if a hit
            # moved its next use further out and left this one behind.
            if self._next_use.get(key) != -negated:
                continue
            del self._next_use[key]
            del self._inserted_at[key]
            return key

        msg = "Opt: evict() called with nothing resident"
        raise RuntimeError(msg)

    def size(self) -> int:
        """Entries currently held. Not part of the interface; used by tests."""
        return len(self._next_use)

    def next_use_of(self, key: K) -> int:
        """Where a resident key is next used, or -1 if it is not held."""
        return self._next_use.get(key, -1)

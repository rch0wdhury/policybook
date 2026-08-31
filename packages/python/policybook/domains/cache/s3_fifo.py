# GENERATED COPY — do not edit. Edit policies/cache/s3-fifo/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""S3-FIFO — three FIFO queues, two bits per entry, no list surgery.

New keys audition in S, a small FIFO. An object reused while it sits there is
promoted to M, the main FIFO; one that is not falls out and leaves its key in G,
a ghost queue. A key returning while its ghost is live skips the audition. Inside
M a two-bit counter grants up to three second chances.

Mirrors ``index.ts``, including the one adaptation from the paper documented in
``README.md``: eviction returns exactly one victim per call.
"""

from __future__ import annotations

from collections import OrderedDict, deque
from typing import Any, Generic, Literal, TypeVar

K = TypeVar("K")

DEFAULT_CAPACITY = 1000
DEFAULT_SMALL_FRACTION = 0.1

MAX_FREQUENCY = 3
"""Two bits per entry: three second chances and no more."""

PROMOTION_THRESHOLD = 1
"""An entry must have been requested more than once in S to be promoted."""

S3FifoQueue = Literal["small", "main", "ghost", "absent"]


class S3Fifo(Generic[K]):
    """Evict from the small queue first, and the main queue only after."""

    __slots__ = (
        "_capacity",
        "_frequency",
        "_ghost",
        "_main",
        "_main_size",
        "_small",
        "_small_size",
    )

    def __init__(
        self,
        capacity: int = DEFAULT_CAPACITY,
        small_fraction: float = DEFAULT_SMALL_FRACTION,
    ) -> None:
        if not isinstance(capacity, int) or isinstance(capacity, bool) or capacity < 2:
            msg = (
                f"S3Fifo: capacity must be an integer of at least 2, received {capacity!r}. "
                "The small and main queues each need at least one entry."
            )
            raise ValueError(msg)
        if not 0 < small_fraction < 1:
            msg = f"S3Fifo: small_fraction must be in (0, 1), received {small_fraction!r}"
            raise ValueError(msg)

        self._capacity = capacity
        self._small_size = max(1, int(capacity * small_fraction))
        self._main_size = capacity - self._small_size

        # Newest appended, so the eviction candidate is the leftmost.
        self._small: deque[K] = deque()
        self._main: deque[K] = deque()
        self._frequency: dict[K, int] = {}
        # The ghost queue remembers as many keys as the main queue holds
        # entries, so a key gets roughly one main-queue lifetime to return.
        self._ghost: OrderedDict[K, None] = OrderedDict()

    def on_access(self, key: K, hit: bool, meta: dict[str, Any] | None = None) -> None:
        """Record an access. A hit bumps a counter and moves nothing."""
        del meta

        if hit:
            if key not in self._frequency:
                msg = f"S3Fifo: on_access reported a hit for a key it does not hold: {key!r}"
                raise RuntimeError(msg)
            self._frequency[key] = min(self._frequency[key] + 1, MAX_FREQUENCY)
            return

        if self.size() > self._capacity:
            msg = (
                f"S3Fifo: {self.size()} entries inserted without an evict, capacity is "
                f"{self._capacity}. Call evict() once the cache is over capacity."
            )
            raise RuntimeError(msg)

        self._frequency[key] = 0
        if key in self._ghost:
            # Falling out of the small queue and coming back is itself evidence
            # of reuse, so the key skips the audition.
            del self._ghost[key]
            self._main.append(key)
            return
        self._small.append(key)

    def evict(self) -> K:
        """Drain the small queue if it is over its share, else the main one."""
        if len(self._small) >= self._small_size:
            evicted = self._evict_from_small()
            if evicted is not None:
                return evicted
        return self._evict_from_main()

    def size(self) -> int:
        """Entries currently held. Not part of the interface; used by tests."""
        return len(self._small) + len(self._main)

    def queue_of(self, key: K) -> S3FifoQueue:
        """Where a key lives right now. Not part of the interface; used by tests."""
        if key in self._frequency:
            return "small" if key in self._small else "main"
        return "ghost" if key in self._ghost else "absent"

    def frequency_of(self, key: K) -> int:
        """The two-bit counter for a key, or 0. Used by tests."""
        return self._frequency.get(key, 0)

    def _evict_from_small(self) -> K | None:
        """Promote what has been reused; evict the first thing that has not."""
        while self._small:
            key = self._small.popleft()
            if self._frequency[key] > PROMOTION_THRESHOLD:
                self._main.append(key)
                continue

            self._remember_ghost(key)
            del self._frequency[key]
            return key
        return None

    def _evict_from_main(self) -> K:
        """Spend each entry's remaining second chances before evicting it."""
        while self._main:
            key = self._main.popleft()
            frequency = self._frequency[key]
            if frequency > 0:
                self._frequency[key] = frequency - 1
                self._main.append(key)
                continue

            del self._frequency[key]
            return key

        # The main queue is empty; fall back to the small one however short.
        fallback = self._evict_from_small()
        if fallback is None:
            msg = "S3Fifo: evict() called with nothing resident"
            raise RuntimeError(msg)
        return fallback

    def _remember_ghost(self, key: K) -> None:
        if len(self._ghost) >= self._main_size:
            self._ghost.popitem(last=False)
        self._ghost[key] = None

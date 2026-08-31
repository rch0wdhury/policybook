# GENERATED COPY — do not edit. Edit policies/cache/w-tinylfu/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""W-TinyLFU — frequency-based admission on four bits per counter.

Approximate counts for far more keys than the cache holds, kept in a fixed-size
count-min sketch and halved periodically so the estimate follows the workload.
That makes frequency cheap enough to use for admission rather than only
eviction: an unpopular newcomer never displaces a proven entry.

A small window LRU absorbs new arrivals; when it overflows, its victim's
estimated frequency is compared with the main cache's victim, and only the more
popular survives. The main cache is a segmented LRU.

Mirrors ``index.ts``, including every sketch parameter — the two must produce
identical estimates from identical accesses.
"""

from __future__ import annotations

from collections import OrderedDict
from typing import Any, Literal

from policybook.rng import mix32

DEFAULT_CAPACITY = 1000
DEFAULT_WINDOW_FRACTION = 0.01
DEFAULT_PROTECTED_FRACTION = 0.8

MASK32 = 0xFFFFFFFF
"""Row salts for the sketch. Arbitrary odd constants with good bit mixing."""
SALT = (0x9E3779B9, 0x85EBCA6B, 0xC2B2AE35, 0x27D4EB2F)
SKETCH_ROWS = 4
MAX_COUNT = 15
"""Counters saturate here; four bits cannot hold more."""

WTinyLfuSegment = Literal["window", "probation", "protected", "absent"]


def _next_power_of_two(value: int) -> int:
    result = 1
    while result < value:
        result *= 2
    return result


class WTinyLfu:
    """Admit a candidate only when the sketch rates it above the incumbent."""

    __slots__ = (
        "_capacity",
        "_doorkeeper",
        "_doorkeeper_mask",
        "_main_size",
        "_probation",
        "_protected",
        "_protected_size",
        "_sample_limit",
        "_sampled",
        "_sketch",
        "_sketch_mask",
        "_sketch_width",
        "_window",
        "_window_size",
    )

    def __init__(
        self,
        capacity: int = DEFAULT_CAPACITY,
        window_fraction: float = DEFAULT_WINDOW_FRACTION,
        protected_fraction: float = DEFAULT_PROTECTED_FRACTION,
    ) -> None:
        if not isinstance(capacity, int) or isinstance(capacity, bool) or capacity < 2:
            msg = (
                f"WTinyLfu: capacity must be an integer of at least 2, received {capacity!r}. "
                "The window and the main cache each need at least one entry."
            )
            raise ValueError(msg)
        if not 0 < window_fraction < 1:
            msg = f"WTinyLfu: window_fraction must be in (0, 1), received {window_fraction!r}"
            raise ValueError(msg)
        if not 0 < protected_fraction < 1:
            msg = (
                f"WTinyLfu: protected_fraction must be in (0, 1), received {protected_fraction!r}"
            )
            raise ValueError(msg)

        self._capacity = capacity
        # The window holds at least one entry, and never the whole cache.
        self._window_size = max(1, int(capacity * window_fraction))
        self._main_size = capacity - self._window_size
        self._protected_size = max(1, int(self._main_size * protected_fraction))

        # Most recently used last in every segment, so the victim is the first.
        self._window: OrderedDict[int, None] = OrderedDict()
        self._probation: OrderedDict[int, None] = OrderedDict()
        self._protected: OrderedDict[int, None] = OrderedDict()

        # Eight sketch positions per cached entry, rounded up to a power of two
        # so the modulo is a mask.
        self._sketch_width = _next_power_of_two(capacity * 8)
        self._sketch_mask = self._sketch_width - 1
        self._sketch = bytearray((SKETCH_ROWS * self._sketch_width) // 2)

        doorkeeper_bits = _next_power_of_two(capacity * 8)
        self._doorkeeper_mask = doorkeeper_bits - 1
        self._doorkeeper = bytearray(doorkeeper_bits // 8)

        # Halve the counters every ten accesses per cached entry, so the
        # estimate tracks the workload instead of accumulating forever.
        self._sample_limit = capacity * 10
        self._sampled = 0

    def on_access(self, key: int, hit: bool, meta: dict[str, Any] | None = None) -> None:
        """Record an access, promoting within the main cache on reuse."""
        del meta
        # Every access is evidence, whether or not the key is resident. Counting
        # misses is what lets a key earn admission before it is ever cached.
        self._record(key)

        if hit:
            if key in self._window:
                self._window.move_to_end(key)
                return
            if key in self._probation:
                # A second hit in the main cache promotes the entry out of reach
                # of the admission contest, which only looks at probation.
                del self._probation[key]
                self._protected[key] = None
                if len(self._protected) > self._protected_size:
                    demoted, _ = self._protected.popitem(last=False)
                    self._probation[demoted] = None
                return
            if key in self._protected:
                self._protected.move_to_end(key)
                return
            msg = f"WTinyLfu: on_access reported a hit for a key it does not hold: {key!r}"
            raise RuntimeError(msg)

        if self.size() > self._capacity:
            msg = (
                f"WTinyLfu: {self.size()} entries inserted without an evict, capacity is "
                f"{self._capacity}. Call evict() once the cache is over capacity."
            )
            raise RuntimeError(msg)

        # New keys always enter the window; admission is contested later.
        self._window[key] = None
        self._drain_window()

    def evict(self) -> int:
        """Run the admission contest, or take the main cache's victim."""
        # Normally a no-op here: the window is drained on insertion.
        self._drain_window()

        if len(self._window) > self._window_size:
            candidate = next(iter(self._window))
            victim = self._main_victim()

            # Strictly greater: on a tie the incumbent stays. A resident entry
            # has demonstrated its frequency while the candidate has only an
            # estimate, and admitting on equal evidence would let a stream of
            # one-hit wonders churn the cache.
            if self._estimate(candidate) > self._estimate(victim):
                del self._window[candidate]
                self._probation[candidate] = None
                self._remove(victim)
                return victim

            del self._window[candidate]
            return candidate

        victim = self._main_victim()
        self._remove(victim)
        return victim

    def size(self) -> int:
        """Entries currently held. Not part of the interface; used by tests."""
        return len(self._window) + len(self._probation) + len(self._protected)

    def segment_of(self, key: int) -> WTinyLfuSegment:
        """Which segment a key is in. Not part of the interface; used by tests."""
        if key in self._window:
            return "window"
        if key in self._probation:
            return "probation"
        if key in self._protected:
            return "protected"
        return "absent"

    def frequency_of(self, key: int) -> int:
        """The sketch's estimate for a key. Not part of the interface; used by tests."""
        return self._estimate(key)

    # --- segments -------------------------------------------------------------

    def _drain_window(self) -> None:
        """Move the window's overflow into the main cache while it has room."""
        main = len(self._probation) + len(self._protected)
        while len(self._window) > self._window_size and main < self._main_size:
            promoted, _ = self._window.popitem(last=False)
            self._probation[promoted] = None
            main += 1

    def _main_victim(self) -> int:
        if self._probation:
            return next(iter(self._probation))
        if self._protected:
            return next(iter(self._protected))
        if self._window:
            return next(iter(self._window))
        msg = "WTinyLfu: evict() called with nothing resident"
        raise RuntimeError(msg)

    def _remove(self, key: int) -> None:
        for segment in (self._window, self._probation, self._protected):
            if key in segment:
                del segment[key]
                return

    # --- the sketch -----------------------------------------------------------

    def _record(self, key: int) -> None:
        """Note an access.

        The doorkeeper absorbs a key's first appearance, so the very large
        number of keys seen exactly once never consume sketch counters at all.
        """
        digest = mix32(key)

        if not self._doorkeeper_test(digest):
            self._doorkeeper_set(digest)
        else:
            for row in range(SKETCH_ROWS):
                self._increment(row * self._sketch_width + self._position(digest, row))

        self._sampled += 1
        if self._sampled >= self._sample_limit:
            self._age()

    def _estimate(self, key: int) -> int:
        """The count-min estimate, plus one if the doorkeeper has seen the key."""
        digest = mix32(key)

        smallest = MAX_COUNT
        for row in range(SKETCH_ROWS):
            count = self._counter_at(row * self._sketch_width + self._position(digest, row))
            smallest = min(smallest, count)

        return smallest + 1 if self._doorkeeper_test(digest) else smallest

    def _position(self, digest: int, row: int) -> int:
        return mix32((digest ^ SALT[row]) & MASK32) & self._sketch_mask

    def _counter_at(self, index: int) -> int:
        byte = self._sketch[index >> 1]
        return byte & 0x0F if index % 2 == 0 else byte >> 4

    def _increment(self, index: int) -> None:
        byte_index = index >> 1
        byte = self._sketch[byte_index]
        if index % 2 == 0:
            value = byte & 0x0F
            if value < MAX_COUNT:
                self._sketch[byte_index] = (byte & 0xF0) | (value + 1)
        else:
            value = byte >> 4
            if value < MAX_COUNT:
                self._sketch[byte_index] = (byte & 0x0F) | ((value + 1) << 4)

    def _age(self) -> None:
        """Halve every counter and forget the doorkeeper.

        This is what stops W-TinyLFU becoming LFU: a key popular an hour ago
        decays instead of holding its place forever. Shifting right and masking
        with 0x77 halves both counters in a byte without letting the high
        nibble's low bit leak into the low one.
        """
        sketch = self._sketch
        for index in range(len(sketch)):
            sketch[index] = (sketch[index] >> 1) & 0x77
        for index in range(len(self._doorkeeper)):
            self._doorkeeper[index] = 0
        self._sampled = 0

    def _doorkeeper_position(self, digest: int, which: int) -> int:
        return mix32((digest ^ SALT[which]) & MASK32) & self._doorkeeper_mask

    def _doorkeeper_test(self, digest: int) -> bool:
        for which in range(2):
            bit = self._doorkeeper_position(digest, which)
            if not self._doorkeeper[bit >> 3] & (1 << (bit & 7)):
                return False
        return True

    def _doorkeeper_set(self, digest: int) -> None:
        for which in range(2):
            bit = self._doorkeeper_position(digest, which)
            self._doorkeeper[bit >> 3] |= 1 << (bit & 7)

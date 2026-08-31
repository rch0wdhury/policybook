"""The ``cache`` domain interface.

Mirrors ``packages/core/src/domains/cache/interface.ts`` method for method;
only the spelling is idiomatic Python (concept.md §12.3).

A cache holds at most ``capacity`` keys. When a new key arrives and the cache is
full, something has to go, and which one is the whole question. A policy
observes every lookup and names a victim when asked. It never sees the cached
values, never reads the clock, and never allocates on the hot path.
"""

from __future__ import annotations

from typing import Protocol, TypedDict, TypeVar

__all__ = ["CacheMeta", "CachePolicy"]

K = TypeVar("K")


class CacheMeta(TypedDict, total=False):
    """Extra information a policy may use, supplied by the harness."""

    size: int
    """Entry size, for size-aware policies. Defaults to 1."""

    now: int
    """Domain time, in arbitrary units. Never wall-clock."""


class CachePolicy(Protocol[K]):
    """Every cache policy implements this, and nothing more.

    A policy may additionally define ``admit(key, meta=None) -> bool`` to
    decline an insertion — W-TinyLFU uses it to stop a one-hit wonder from
    displacing something valuable. It is optional, so it is not part of the
    protocol; the harness checks for it.
    """

    def on_access(self, key: K, hit: bool, meta: CacheMeta | None = None) -> None:
        """Called on every lookup, before insertion on a miss.

        ``hit`` says whether the key was resident. A policy learns everything it
        knows from this call.
        """
        ...

    def evict(self) -> K:
        """Called when capacity is exceeded. Returns the key to remove.

        The returned key must currently be resident.
        """
        ...

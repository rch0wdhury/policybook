# GENERATED COPY — do not edit. Edit policies/kv-cache/sliding-window/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""Sliding window — keep the most recent tokens and forget the rest.

The baseline every other policy in this domain is measured against, and a much
stronger one than it looks: attention is dominated by recency, so a policy that
keeps only the recent tokens still retains most of the mass.

What it gets wrong is everything that is old and still important — above all the
attention sinks at the start of the sequence, which real models return to
constantly regardless of content. That single observation is the whole of
``streaming-llm``, which is this policy plus four positions.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

from collections import deque
from collections.abc import Sequence

from policybook.rng import Rng

DEFAULT_BUDGET = 512


class SlidingWindow:
    """Keeps the most recent ``budget`` token positions."""

    __slots__ = ("_kept", "_victims")

    def __init__(self, budget: int = DEFAULT_BUDGET, rng: Rng | None = None) -> None:
        # `rng` is accepted and ignored: every policy in the registry is
        # constructed the same way, and this one is entirely deterministic.
        del rng

        if not isinstance(budget, int) or isinstance(budget, bool) or budget < 1:
            msg = f"SlidingWindow: budget must be a positive integer, received {budget!r}"
            raise ValueError(msg)

        # A deque rather than a list: eviction pops from the left, which is O(n)
        # on a list and O(1) here. `maxlen` is deliberately not set — silently
        # discarding the oldest entry would drop a position without ever
        # reporting it to the harness, which is the one thing a policy here must
        # never do.
        #
        # Position 0's token exists before the first decode step, so the cache
        # holds it from the outset (see the domain interface).
        self._kept: deque[int] = deque([0])
        self._victims: list[int] = []

    def on_decode_step(self, pos: int, attn: Sequence[float] | None = None) -> None:
        """Note the new token. The attention is accepted and ignored.

        Not reading it is the point: this policy cannot be accused of using
        information it does not have.
        """
        del attn
        self._kept.append(pos)

    def evict(self, budget: int) -> list[int]:
        """Drop the oldest positions until the budget is met.

        The returned list is reused between calls, which the domain interface
        permits: the harness consumes it before the policy is called again.
        """
        victims = self._victims
        victims.clear()

        kept = self._kept
        while len(kept) > budget:
            victims.append(kept.popleft())

        return victims

    def kept_count(self) -> int:
        """How many positions are currently held."""
        return len(self._kept)

"""StreamingLLM — a sliding window that also pins the first few tokens.

Xiao et al. noticed that a large, roughly content-independent share of every
attention distribution lands on the first few tokens of the sequence — not
because those tokens matter, but because softmax has to put its mass somewhere
and the earliest positions are visible from everywhere. They called them
**attention sinks**.

Evict them and generation degrades immediately rather than gradually, which is
exactly what a plain sliding window does, since they are the oldest thing it
holds. This policy is that one plus four pinned positions, and it recovers most
of the quality gap for a fixed four slots and no arithmetic.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

from collections import deque
from collections.abc import Sequence

from policybook.rng import Rng

DEFAULT_BUDGET = 512
DEFAULT_SINKS = 4
"""Four, from the paper.

Xiao et al. measure the recovery as a function of this and find it flat from
about four onward: the first token carries most of the sink mass.
"""


class StreamingLlm:
    """Keeps the first ``sinks`` positions plus the most recent ones."""

    __slots__ = ("_sinks", "_sinks_held", "_victims", "_window")

    def __init__(
        self,
        budget: int = DEFAULT_BUDGET,
        sinks: int = DEFAULT_SINKS,
        rng: Rng | None = None,
    ) -> None:
        # `rng` is accepted and ignored: this policy is entirely deterministic.
        del rng

        if not isinstance(budget, int) or isinstance(budget, bool) or budget < 1:
            msg = f"StreamingLlm: budget must be a positive integer, received {budget!r}"
            raise ValueError(msg)
        if not isinstance(sinks, int) or isinstance(sinks, bool) or sinks < 0:
            msg = f"StreamingLlm: sinks must be a non-negative integer, received {sinks!r}"
            raise ValueError(msg)
        if sinks >= budget:
            msg = (
                f"StreamingLlm: sinks ({sinks}) must be smaller than budget ({budget}), "
                "or there is no room for a recency window."
            )
            raise ValueError(msg)

        self._sinks = sinks
        self._sinks_held = 0
        self._window: deque[int] = deque()
        self._victims: list[int] = []

        # Position 0's token exists before the first decode step. It is a sink
        # when any are configured, and otherwise the first window entry.
        if sinks > 0:
            self._sinks_held = 1
        else:
            self._window.append(0)

    def on_decode_step(self, pos: int, attn: Sequence[float] | None = None) -> None:
        """Note the new token, as a sink or as part of the recency window.

        The attention is accepted and ignored: this policy pins the
        *structurally* special positions, not the important ones.
        """
        del attn

        if pos < self._sinks:
            self._sinks_held += 1
            return
        self._window.append(pos)

    def evict(self, budget: int) -> list[int]:
        """Drop the oldest non-sink positions until the budget is met.

        The returned list is reused between calls, which the domain interface
        permits: the harness consumes it before the policy is called again.
        """
        victims = self._victims
        victims.clear()

        window = self._window
        while self._sinks_held + len(window) > budget and window:
            victims.append(window.popleft())

        return victims

    def kept_count(self) -> int:
        """How many positions are currently held, sinks included."""
        return self._sinks_held + len(self._window)

    def sink_count(self) -> int:
        """How many pinned sink positions are held."""
        return self._sinks_held

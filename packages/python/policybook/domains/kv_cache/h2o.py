# GENERATED COPY — do not edit. Edit policies/kv-cache/h2o/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""H2O — keep the tokens that have received the most attention so far.

The first policy in this domain that reads the attention weights. StreamingLLM
pins the *structurally* special positions — the first few, which absorb
attention regardless of content. This one finds the *important* ones.

Zhang et al. observed that attention is not merely sparse but persistently
sparse: a small set of positions accumulates most of the attention across a
generation, and which positions those are is fairly stable. They called them
heavy hitters, and the policy follows — sum each position's attention over time
and evict the smallest sum.

A recent window is held back from the scoring, because a token generated two
steps ago has had no opportunity to accumulate anything. That is not a
refinement but load-bearing: without it the policy evicts the newest token every
step and the cache never advances.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

from collections.abc import Sequence

from policybook.rng import Rng

DEFAULT_BUDGET = 512
DEFAULT_RECENT_WINDOW = 32
"""Thirty-two, following the paper's split between heavy hitters and recency.

The exact number matters less than that it is not zero: a token generated this
step has accumulated nothing, and without protection would be evicted first
every time.
"""


class H2o:
    """Keeps the highest cumulative-attention positions, plus a recent window."""

    __slots__ = ("_capacity", "_positions", "_recent_window", "_scores", "_victims")

    def __init__(
        self,
        budget: int = DEFAULT_BUDGET,
        recent_window: int = DEFAULT_RECENT_WINDOW,
        rng: Rng | None = None,
    ) -> None:
        # `rng` is accepted and ignored: this policy is entirely deterministic.
        del rng

        if not isinstance(budget, int) or isinstance(budget, bool) or budget < 1:
            msg = f"H2o: budget must be a positive integer, received {budget!r}"
            raise ValueError(msg)
        if (
            not isinstance(recent_window, int)
            or isinstance(recent_window, bool)
            or recent_window < 0
        ):
            msg = (
                "H2o: recent_window must be a non-negative integer, received "
                f"{recent_window!r}"
            )
            raise ValueError(msg)
        if recent_window >= budget:
            msg = (
                f"H2o: recent_window ({recent_window}) must be smaller than budget "
                f"({budget}), or there is nothing the score is allowed to choose between."
            )
            raise ValueError(msg)

        # One more than the budget, as the reference sizes its arrays: the kept
        # set stands at budget + 1 between a decode step and its evict, never
        # beyond.
        self._capacity = budget + 1
        self._recent_window = recent_window
        # Two parallel lists rather than a list of pairs: the attention arrives
        # as a flat sequence in the same order, so index alignment is the whole
        # data structure.
        #
        # Position 0's token exists before the first decode step (see the domain
        # interface), and starts with no attention to its name.
        self._positions: list[int] = [0]
        self._scores: list[float] = [0.0]
        self._victims: list[int] = []

    def on_decode_step(self, pos: int, attn: Sequence[float] | None = None) -> None:
        """Add this step's attention to each kept position's running total.

        ``attn[i]`` belongs to the i-th kept position in ascending order, which
        is exactly how ``_positions`` is stored, so the two are index-aligned.

        Scores accumulate in float64 while the weights arrive as float32, which
        keeps the sum exact for far longer and identical across the three
        implementations.
        """
        if attn is not None:
            scores = self._scores
            for i in range(min(len(attn), len(scores))):
                scores[i] = scores[i] + attn[i]

        if len(self._positions) == self._capacity:
            msg = (
                f"H2o: asked to hold {len(self._positions) + 1} positions with a "
                f"budget of {self._capacity - 1}. The harness's budget must match "
                "the policy's."
            )
            raise RuntimeError(msg)

        self._positions.append(pos)
        self._scores.append(0.0)

    def evict(self, budget: int) -> list[int]:
        """Drop the lowest-scoring positions outside the recent window.

        The returned list is reused between calls, which the domain interface
        permits: the harness consumes it before the policy is called again.
        """
        victims = self._victims
        victims.clear()

        size = len(self._positions)
        needed = size - budget
        if needed <= 0:
            return victims

        # The recent window is the tail of the ascending list, so everything
        # before `evictable_end` is fair game and nothing after it is.
        evictable_end = size - min(self._recent_window, size)

        positions = self._positions
        scores = self._scores
        doomed: set[int] = set()

        # Repeated argmin rather than a sort: in steady state exactly one
        # position goes per step, so this is a single linear scan.
        for _ in range(min(needed, evictable_end)):
            best = -1
            for i in range(evictable_end):
                if i in doomed:
                    continue
                # Strictly less, so a tie leaves the earlier index standing —
                # the lower position, since the list is ascending.
                if best == -1 or scores[i] < scores[best]:
                    best = i
            if best == -1:
                break
            doomed.add(best)

        # One compacting pass: victims come out in ascending position order.
        kept_positions: list[int] = []
        kept_scores: list[float] = []
        for i in range(size):
            if i in doomed:
                victims.append(positions[i])
                continue
            kept_positions.append(positions[i])
            kept_scores.append(scores[i])

        self._positions = kept_positions
        self._scores = kept_scores
        return victims

    def kept_count(self) -> int:
        """How many positions are currently held."""
        return len(self._positions)

    def score_of(self, pos: int) -> float:
        """The cumulative attention ``pos`` has received, or -1 if not held.

        Reporting only — it exists so vectors can pin the score arithmetic
        directly rather than inferring it from which position was evicted.
        """
        for i, position in enumerate(self._positions):
            if position == pos:
                return self._scores[i]
        return -1.0

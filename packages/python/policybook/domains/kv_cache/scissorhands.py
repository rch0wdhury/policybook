# GENERATED COPY — do not edit. Edit policies/kv-cache/scissorhands/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""Scissorhands — count how many steps a token mattered for, not how much.

Liu et al. proposed the persistence of importance hypothesis: a token that was
influential at one decoding step tends to keep being influential at later ones.
If that holds, the useful question is not *how much* attention a position has
collected but *how reliably* it attracts any.

So a position earns a vote on every step where its attention exceeds its fair
share — ``1 / kept`` — and the fewest votes is what gets evicted.

The difference from H2O is the difference between a sum and a count, and it
decides one thing: what happens to a position that was enormously important once
and irrelevant since. H2O's cumulative score never forgets; here that position
has a single vote and loses to anything that has quietly cleared its share a few
times.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

from collections.abc import Sequence

from policybook.rng import Rng

DEFAULT_BUDGET = 512
DEFAULT_RECENT_WINDOW = 32
"""Thirty-two, matching H2O so the two are comparable."""


class Scissorhands:
    """Keeps the positions that most often beat their share of the attention."""

    __slots__ = ("_capacity", "_positions", "_recent_window", "_victims", "_votes")

    def __init__(
        self,
        budget: int = DEFAULT_BUDGET,
        recent_window: int = DEFAULT_RECENT_WINDOW,
        rng: Rng | None = None,
    ) -> None:
        # `rng` is accepted and ignored: this policy is entirely deterministic.
        del rng

        if not isinstance(budget, int) or isinstance(budget, bool) or budget < 1:
            msg = f"Scissorhands: budget must be a positive integer, received {budget!r}"
            raise ValueError(msg)
        if (
            not isinstance(recent_window, int)
            or isinstance(recent_window, bool)
            or recent_window < 0
        ):
            msg = (
                "Scissorhands: recent_window must be a non-negative integer, received "
                f"{recent_window!r}"
            )
            raise ValueError(msg)
        if recent_window >= budget:
            msg = (
                f"Scissorhands: recent_window ({recent_window}) must be smaller than "
                f"budget ({budget}), or there is nothing the votes are allowed to "
                "choose between."
            )
            raise ValueError(msg)

        # One more than the budget, as the reference sizes its arrays: the kept
        # set stands at budget + 1 between a decode step and its evict, never
        # beyond.
        self._capacity = budget + 1
        self._recent_window = recent_window
        # Position 0's token exists before the first decode step (see the domain
        # interface), and has voted on nothing yet.
        self._positions: list[int] = [0]
        self._votes: list[int] = [0]
        self._victims: list[int] = []

    def on_decode_step(self, pos: int, attn: Sequence[float] | None = None) -> None:
        """Give a vote to every kept position that beat its fair share this step.

        The threshold is ``1 / len(attn)`` — what each position would receive if
        this step's attention were spread evenly over everything held. The
        comparison is **strict**, so a position that exactly matches its share
        does not vote; at the first step, where one position holds all the
        attention and its share is 1, that means no vote at all.
        """
        if attn is not None and len(attn) > 0:
            # One float64 division per step, not per position.
            share = 1 / len(attn)
            votes = self._votes
            for i in range(min(len(attn), len(votes))):
                if attn[i] > share:
                    votes[i] = votes[i] + 1

        if len(self._positions) == self._capacity:
            msg = (
                f"Scissorhands: asked to hold {len(self._positions) + 1} positions "
                f"with a budget of {self._capacity - 1}. The harness's budget must "
                "match the policy's."
            )
            raise RuntimeError(msg)

        self._positions.append(pos)
        self._votes.append(0)

    def evict(self, budget: int) -> list[int]:
        """Drop the least persistent positions outside the recent window.

        The returned list is reused between calls, which the domain interface
        permits: the harness consumes it before the policy is called again.
        """
        victims = self._victims
        victims.clear()

        size = len(self._positions)
        needed = size - budget
        if needed <= 0:
            return victims

        evictable_end = size - min(self._recent_window, size)

        positions = self._positions
        votes = self._votes
        doomed: set[int] = set()

        # Repeated argmin rather than a sort: in steady state exactly one
        # position goes per step, so this is a single linear scan.
        for _ in range(min(needed, evictable_end)):
            best = -1
            for i in range(evictable_end):
                if i in doomed:
                    continue
                # Strictly less, so a tie leaves the earlier index standing —
                # the lower position, since the list is ascending. Ties are
                # common here: vote counts are small integers.
                if best == -1 or votes[i] < votes[best]:
                    best = i
            if best == -1:
                break
            doomed.add(best)

        # One compacting pass: victims come out in ascending position order.
        kept_positions: list[int] = []
        kept_votes: list[int] = []
        for i in range(size):
            if i in doomed:
                victims.append(positions[i])
                continue
            kept_positions.append(positions[i])
            kept_votes.append(votes[i])

        self._positions = kept_positions
        self._votes = kept_votes
        return victims

    def kept_count(self) -> int:
        """How many positions are currently held."""
        return len(self._positions)

    def votes_of(self, pos: int) -> int:
        """How many steps ``pos`` beat its fair share on, or -1 if not held.

        Reporting only — it exists so vectors can pin the voting rule directly,
        including the strictness of the comparison.
        """
        for i, position in enumerate(self._positions):
            if position == pos:
                return self._votes[i]
        return -1

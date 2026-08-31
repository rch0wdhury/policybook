# GENERATED COPY — do not edit. Edit policies/kv-cache/tova/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""TOVA — drop whichever token the model just stopped looking at.

Oren et al. argued that a decoder-only transformer is really a multi-state RNN
whose state is the KV cache. From that angle the eviction rule is obvious: at
each step the model tells you, through its attention, which states it is
currently using. Drop the least-used one.

The whole policy is one line, and what makes it distinctive is what it leaves
out. There is no accumulation, so nothing a position did earlier defends it.
H2O sums attention over all time and will protect a position that mattered a
thousand steps ago; this policy has no memory of that at all.

There is no recent-window protection, deliberately: recency is already in the
signal, because recent tokens attract high attention *now*.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

from collections.abc import Sequence

from policybook.rng import Rng

DEFAULT_BUDGET = 512

UNOBSERVED = -1.0
"""Marks a position that has never appeared in an attention vector.

Attention weights are non-negative, so a negative value is unambiguous.
"""


class Tova:
    """Keeps the positions the model attended to most on the latest step."""

    __slots__ = ("_capacity", "_last_attn", "_positions", "_victims")

    def __init__(self, budget: int = DEFAULT_BUDGET, rng: Rng | None = None) -> None:
        # `rng` is accepted and ignored: this policy is entirely deterministic.
        del rng

        if not isinstance(budget, int) or isinstance(budget, bool) or budget < 1:
            msg = f"Tova: budget must be a positive integer, received {budget!r}"
            raise ValueError(msg)

        # One more than the budget, as the reference sizes its arrays: the kept
        # set stands at budget + 1 between a decode step and its evict, never
        # beyond.
        self._capacity = budget + 1
        # Position 0's token exists before the first decode step (see the domain
        # interface) and nothing has attended to it yet.
        self._positions: list[int] = [0]
        self._last_attn: list[float] = [UNOBSERVED]
        self._victims: list[int] = []

    def on_decode_step(self, pos: int, attn: Sequence[float] | None = None) -> None:
        """Record this step's attention, replacing whatever was there before.

        No accumulation: the previous value is discarded outright, which is the
        entire policy.

        The token being generated is admitted as **unobserved**, because nothing
        has attended to it yet. An unobserved position is never evicted, so the
        newest token is safe for exactly one eviction. That is not a recency
        rule smuggled back in; it is a refusal to rank a position on evidence
        that does not exist yet.
        """
        if attn is not None:
            last = self._last_attn
            for i in range(min(len(attn), len(last))):
                last[i] = attn[i]

        if len(self._positions) == self._capacity:
            msg = (
                f"Tova: asked to hold {len(self._positions) + 1} positions with a "
                f"budget of {self._capacity - 1}. The harness's budget must match "
                "the policy's."
            )
            raise RuntimeError(msg)

        self._positions.append(pos)
        self._last_attn.append(UNOBSERVED)

    def evict(self, budget: int) -> list[int]:
        """Drop the positions the model attended to least on the latest step.

        The returned list is reused between calls, which the domain interface
        permits: the harness consumes it before the policy is called again.
        """
        victims = self._victims
        victims.clear()

        size = len(self._positions)
        needed = size - budget
        if needed <= 0:
            return victims

        positions = self._positions
        last = self._last_attn
        doomed: set[int] = set()

        for _ in range(needed):
            best = -1
            for i in range(size):
                if i in doomed:
                    continue
                # An unobserved position is not a candidate: it has no weight to
                # be ranked on, and treating its absence as zero would evict
                # every token the step it was generated.
                if last[i] == UNOBSERVED:
                    continue
                # Strictly less, so a tie leaves the earlier index standing —
                # the lower position, since the list is ascending.
                if best == -1 or last[i] < last[best]:
                    best = i
            if best == -1:
                break
            doomed.add(best)

        # One compacting pass: victims come out in ascending position order.
        kept_positions: list[int] = []
        kept_last: list[float] = []
        for i in range(size):
            if i in doomed:
                victims.append(positions[i])
                continue
            kept_positions.append(positions[i])
            kept_last.append(last[i])

        self._positions = kept_positions
        self._last_attn = kept_last
        return victims

    def kept_count(self) -> int:
        """How many positions are currently held."""
        return len(self._positions)

    def last_attention_of(self, pos: int) -> float:
        """The attention ``pos`` received on the latest step it was present for.

        Returns -1 when the position is not held, or is held but has never been
        attended to. Reporting only.
        """
        for i, position in enumerate(self._positions):
            if position == pos:
                return self._last_attn[i]
        return UNOBSERVED

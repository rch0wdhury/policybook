# GENERATED COPY — do not edit. Edit policies/kv-cache/snapkv/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""SnapKV — score on the last few steps, then max-pool across neighbours.

Two ideas, and the second is the one nothing else here has.

A forgetting window: the score is the attention a position received over the
last ``obs_window`` steps only, which puts this between H2O, whose cumulative
sum never forgets, and TOVA, which remembers exactly one step.

A max-pool across positions: before choosing victims, each position's score is
replaced by the maximum over its ``pool_kernel`` neighbours in the kept order.
Li et al. added this because selecting tokens purely on individual scores
fragments the context — the model attends to a phrase, the peak lands on one
token of it, and evicting the rest leaves a fragment worse than useless.

On the adaptation: SnapKV in the paper is a prefill algorithm. Here the
observation window is the last ``obs_window`` decode steps and the pooling runs
over adjacent *kept* positions, which after eviction are not necessarily
adjacent tokens. The mechanism is the paper's; the setting is not.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

from collections.abc import Sequence

from policybook.rng import Rng

DEFAULT_BUDGET = 512
DEFAULT_RECENT_WINDOW = 32
DEFAULT_OBS_WINDOW = 16
DEFAULT_POOL_KERNEL = 7


class SnapKv:
    """Keeps the positions scoring highest over a pooled observation window."""

    __slots__ = (
        "_capacity",
        "_history",
        "_obs_window",
        "_pool_radius",
        "_positions",
        "_recent_window",
        "_slot",
        "_victims",
    )

    def __init__(
        self,
        budget: int = DEFAULT_BUDGET,
        recent_window: int = DEFAULT_RECENT_WINDOW,
        obs_window: int = DEFAULT_OBS_WINDOW,
        pool_kernel: int = DEFAULT_POOL_KERNEL,
        rng: Rng | None = None,
    ) -> None:
        # `rng` is accepted and ignored: this policy is entirely deterministic.
        del rng

        if not isinstance(budget, int) or isinstance(budget, bool) or budget < 1:
            msg = f"SnapKv: budget must be a positive integer, received {budget!r}"
            raise ValueError(msg)
        if (
            not isinstance(recent_window, int)
            or isinstance(recent_window, bool)
            or recent_window < 0
        ):
            msg = (
                "SnapKv: recent_window must be a non-negative integer, received "
                f"{recent_window!r}"
            )
            raise ValueError(msg)
        if recent_window >= budget:
            msg = (
                f"SnapKv: recent_window ({recent_window}) must be smaller than budget "
                f"({budget}), or there is nothing the score is allowed to choose between."
            )
            raise ValueError(msg)
        if not isinstance(obs_window, int) or isinstance(obs_window, bool) or obs_window < 1:
            msg = f"SnapKv: obs_window must be a positive integer, received {obs_window!r}"
            raise ValueError(msg)
        # An even kernel has no centre, so the neighbours of a position would be
        # lopsided and the pooling would drift in one direction.
        if (
            not isinstance(pool_kernel, int)
            or isinstance(pool_kernel, bool)
            or pool_kernel < 1
            or pool_kernel % 2 == 0
        ):
            msg = (
                "SnapKv: pool_kernel must be a positive odd integer, received "
                f"{pool_kernel!r}"
            )
            raise ValueError(msg)

        self._recent_window = recent_window
        self._obs_window = obs_window
        self._pool_radius = (pool_kernel - 1) // 2
        # One more than the budget, as the reference sizes its arrays: the kept
        # set stands at budget + 1 between a decode step and its evict, never
        # beyond.
        self._capacity = budget + 1
        self._slot = 0

        # Position 0's token exists before the first decode step (see the domain
        # interface), with an empty history.
        self._positions: list[int] = [0]
        self._history: list[list[float]] = [[0.0] * obs_window]
        self._victims: list[int] = []

    def on_decode_step(self, pos: int, attn: Sequence[float] | None = None) -> None:
        """Record this step's attention into the ring, ageing out the oldest.

        Every kept position writes to the same slot, so one counter serves them
        all, and a weight falls out of the window when the ring wraps onto it
        ``obs_window`` observations later.

        **A null attention vector is entirely inert** — nothing is written and
        the ring does not advance — so the window spans the last ``obs_window``
        *observed* steps rather than the last ``obs_window`` calls. Advancing
        without writing would leave a stale weight in the slot for another full
        cycle, so a window claiming to cover the recent past would quietly sum
        values of indeterminate age.
        """
        if attn is not None:
            history = self._history
            slot = self._slot
            for i in range(min(len(attn), len(history))):
                history[i][slot] = attn[i]

            self._slot += 1
            if self._slot == self._obs_window:
                self._slot = 0

        if len(self._positions) == self._capacity:
            msg = (
                f"SnapKv: asked to hold {len(self._positions) + 1} positions with a "
                f"budget of {self._capacity - 1}. The harness's budget must match "
                "the policy's."
            )
            raise RuntimeError(msg)

        self._positions.append(pos)
        self._history.append([0.0] * self._obs_window)

    def _window_sums(self) -> list[float]:
        """Sum each position's window from scratch.

        Adding and subtracting from a running total would drift, and the drift
        would have to be bit-identical in three languages to stay reproducible.
        Slot order is fixed — index 0 upward, not chronological — which is
        arbitrary but pinned, and identical everywhere.
        """
        sums: list[float] = []
        for record in self._history:
            total = 0.0
            for value in record:
                total += value
            sums.append(total)
        return sums

    def evict(self, budget: int) -> list[int]:
        """Drop the lowest-pooled-scoring positions outside the recent window.

        The returned list is reused between calls, which the domain interface
        permits: the harness consumes it before the policy is called again.
        """
        victims = self._victims
        victims.clear()

        size = len(self._positions)
        needed = size - budget
        if needed <= 0:
            return victims

        sums = self._window_sums()

        # Max-pool across neighbours, over the whole kept set: a protected
        # recent position is still a legitimate neighbour to inherit from.
        radius = self._pool_radius
        pooled: list[float] = []
        for i in range(size):
            lo = max(0, i - radius)
            hi = min(size - 1, i + radius)
            best = sums[lo]
            for j in range(lo + 1, hi + 1):
                if sums[j] > best:
                    best = sums[j]
            pooled.append(best)

        evictable_end = size - min(self._recent_window, size)
        doomed: set[int] = set()

        for _ in range(min(needed, evictable_end)):
            best_index = -1
            for i in range(evictable_end):
                if i in doomed:
                    continue
                # Strictly less, so a tie leaves the earlier index standing —
                # the lower position, since the list is ascending.
                if best_index == -1 or pooled[i] < pooled[best_index]:
                    best_index = i
            if best_index == -1:
                break
            doomed.add(best_index)

        # One compacting pass: victims come out in ascending position order, and
        # each survivor's whole history moves with it.
        kept_positions: list[int] = []
        kept_history: list[list[float]] = []
        for i in range(size):
            if i in doomed:
                victims.append(self._positions[i])
                continue
            kept_positions.append(self._positions[i])
            kept_history.append(self._history[i])

        self._positions = kept_positions
        self._history = kept_history
        return victims

    def kept_count(self) -> int:
        """How many positions are currently held."""
        return len(self._positions)

    def window_score_of(self, pos: int) -> float:
        """The attention ``pos`` received over the observation window, unpooled.

        Reporting only — it exists so vectors can pin the windowing arithmetic
        separately from the pooling.
        """
        for i, position in enumerate(self._positions):
            if position == pos:
                total = 0.0
                for value in self._history[i]:
                    total += value
                return total
        return -1.0

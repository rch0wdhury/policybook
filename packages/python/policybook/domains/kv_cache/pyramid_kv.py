# GENERATED COPY — do not edit. Edit policies/kv-cache/pyramidkv/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""PyramidKV — spend more cache on early layers than late ones.

Every other policy in this domain decides *which* tokens to keep. This one
decides *how many*, and leaves the choosing to a SnapKV-style rule underneath.

Cai et al. observed what they called pyramidal information funnelling: attention
in the early layers of a transformer is broad and fairly uniform, while in later
layers it concentrates sharply onto a few positions. A uniform per-layer budget
therefore overfeeds the deep layers and starves the shallow ones.

On a single-layer workload this policy is exactly SnapKV, by arithmetic rather
than by hedge: with ``num_layers=1`` there is nothing to redistribute. The
registry's trace is single-layer, so the benchmark cannot distinguish them.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

from collections.abc import Sequence

from policybook.rng import Rng

DEFAULT_BUDGET = 512
DEFAULT_LAYER = 0
DEFAULT_NUM_LAYERS = 1
DEFAULT_PYRAMID_RATIO = 4
DEFAULT_RECENT_WINDOW = 32
DEFAULT_OBS_WINDOW = 16
DEFAULT_POOL_KERNEL = 7


def pyramid_budget(budget: int, layer: int, num_layers: int, pyramid_ratio: int) -> int:
    """How much cache ``layer`` gets when ``num_layers`` share ``budget`` on average.

    An arithmetic sequence from ``2*budget*r/(r+1)`` down to ``2*budget/(r+1)``,
    whose mean is ``budget`` by construction, evaluated as a single integer
    division so all three implementations floor at the same point::

        effective(k) = 2*budget*( r*(L-1) - k*(r-1) ) // ( (r+1)*(L-1) )

    Public because the allocation is the policy's actual contribution, and a
    caller sizing a real multi-layer cache needs it before constructing
    anything.
    """
    # One layer has nothing to redistribute, and the denominator would be zero.
    if num_layers <= 1:
        return budget

    span = num_layers - 1
    numerator = 2 * budget * (pyramid_ratio * span - layer * (pyramid_ratio - 1))
    denominator = (pyramid_ratio + 1) * span
    return numerator // denominator


class PyramidKv:
    """SnapKV selection under a per-layer budget that shrinks with depth."""

    __slots__ = (
        "_capacity",
        "_effective",
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
        layer: int = DEFAULT_LAYER,
        num_layers: int = DEFAULT_NUM_LAYERS,
        pyramid_ratio: int = DEFAULT_PYRAMID_RATIO,
        recent_window: int = DEFAULT_RECENT_WINDOW,
        obs_window: int = DEFAULT_OBS_WINDOW,
        pool_kernel: int = DEFAULT_POOL_KERNEL,
        rng: Rng | None = None,
    ) -> None:
        # `rng` is accepted and ignored: this policy is entirely deterministic.
        del rng

        if not isinstance(budget, int) or isinstance(budget, bool) or budget < 1:
            msg = f"PyramidKv: budget must be a positive integer, received {budget!r}"
            raise ValueError(msg)
        if not isinstance(num_layers, int) or isinstance(num_layers, bool) or num_layers < 1:
            msg = f"PyramidKv: num_layers must be a positive integer, received {num_layers!r}"
            raise ValueError(msg)
        if (
            not isinstance(layer, int)
            or isinstance(layer, bool)
            or layer < 0
            or layer >= num_layers
        ):
            msg = (
                f"PyramidKv: layer must be an integer in [0, {num_layers}), "
                f"received {layer!r}"
            )
            raise ValueError(msg)
        # A ratio below one would invert the pyramid, which is a different
        # policy; exactly one is the degenerate uniform case.
        if (
            not isinstance(pyramid_ratio, int)
            or isinstance(pyramid_ratio, bool)
            or pyramid_ratio < 1
        ):
            msg = (
                "PyramidKv: pyramid_ratio must be an integer of at least 1, received "
                f"{pyramid_ratio!r}"
            )
            raise ValueError(msg)
        if (
            not isinstance(recent_window, int)
            or isinstance(recent_window, bool)
            or recent_window < 0
        ):
            msg = (
                "PyramidKv: recent_window must be a non-negative integer, received "
                f"{recent_window!r}"
            )
            raise ValueError(msg)
        if recent_window >= budget:
            msg = (
                f"PyramidKv: recent_window ({recent_window}) must be smaller than budget "
                f"({budget}), or there is nothing the score is allowed to choose between."
            )
            raise ValueError(msg)
        if not isinstance(obs_window, int) or isinstance(obs_window, bool) or obs_window < 1:
            msg = f"PyramidKv: obs_window must be a positive integer, received {obs_window!r}"
            raise ValueError(msg)
        if (
            not isinstance(pool_kernel, int)
            or isinstance(pool_kernel, bool)
            or pool_kernel < 1
            or pool_kernel % 2 == 0
        ):
            msg = (
                "PyramidKv: pool_kernel must be a positive odd integer, received "
                f"{pool_kernel!r}"
            )
            raise ValueError(msg)

        # A deep layer's share can fall below the recent window, at which point
        # the cache would be smaller than its own protected region — a state the
        # selection rule cannot express. It keeps the window plus one instead.
        allocated = pyramid_budget(budget, layer, num_layers, pyramid_ratio)
        self._effective = max(allocated, recent_window + 1)

        self._recent_window = recent_window
        self._obs_window = obs_window
        self._pool_radius = (pool_kernel - 1) // 2
        # Room for whichever cap is larger: a shallow layer's share exceeds the
        # average, and a caller may still drive this at the average budget.
        self._capacity = max(self._effective, budget) + 1
        self._slot = 0

        self._positions: list[int] = [0]
        self._history: list[list[float]] = [[0.0] * obs_window]
        self._victims: list[int] = []

    def on_decode_step(self, pos: int, attn: Sequence[float] | None = None) -> None:
        """Record this step's attention into the ring. See SnapKV for the mechanism."""
        if attn is not None:
            history = self._history
            slot = self._slot
            for i in range(min(len(attn), len(history))):
                history[i][slot] = attn[i]

            # A null vector is inert, ring included.
            self._slot += 1
            if self._slot == self._obs_window:
                self._slot = 0

        if len(self._positions) == self._capacity:
            msg = (
                f"PyramidKv: asked to hold {len(self._positions) + 1} positions "
                f"with room for {self._capacity - 1}. The caller's budget must "
                "match the policy's."
            )
            raise RuntimeError(msg)

        self._positions.append(pos)
        self._history.append([0.0] * self._obs_window)

    def evict(self, budget: int) -> list[int]:
        """Drop the lowest-pooled-scoring positions outside the recent window.

        The target is the tighter of the caller's budget and this layer's share,
        so a deep layer holds less than it was offered — which is the whole
        point — while never exceeding what the caller asked for.
        """
        victims = self._victims
        victims.clear()

        size = len(self._positions)
        target = min(budget, self._effective)
        needed = size - target
        if needed <= 0:
            return victims

        sums: list[float] = []
        for record in self._history:
            total = 0.0
            for value in record:
                total += value
            sums.append(total)

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
                if best_index == -1 or pooled[i] < pooled[best_index]:
                    best_index = i
            if best_index == -1:
                break
            doomed.add(best_index)

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

    def effective_budget(self) -> int:
        """This layer's share of the budget after redistribution.

        Reporting only, and the number the whole policy turns on.
        """
        return self._effective

    def window_score_of(self, pos: int) -> float:
        """The attention ``pos`` received over the observation window, unpooled."""
        for i, position in enumerate(self._positions):
            if position == pos:
                total = 0.0
                for value in self._history[i]:
                    total += value
                return total
        return -1.0

"""Zipf sampling for the canonical traces.

The Python side of ``packages/core/src/zipf.ts``. It must produce the same
ranks from the same seed, draw for draw, or the traces diverge and the benchmark
numbers stop being comparable (concept.md §10).

**Why only two exponents.** A Zipf weight is ``1 / rank**alpha``, which wants
``pow``. ``pow`` is not correctly rounded — the same call can return different
doubles on different C standard libraries — and a trace that differs by one ULP
eventually samples a different key. The two supported exponents are the two that
need only ``sqrt``, which IEEE-754 requires to be correctly rounded:

    alpha = 1.00 -> 1 / r
    alpha = 0.75 -> 1 / (sqrt(r) * sqrt(sqrt(r)))    since r**0.75 = r**(1/2) * r**(1/4)

See ``packages/core/src/domains/cache/TRACES.md`` for the full specification.
"""

from __future__ import annotations

import math

from policybook.rng import Rng

__all__ = ["SUPPORTED_ALPHAS", "ZipfSampler", "zipf_weight"]

SUPPORTED_ALPHAS = (1.0, 0.75)
"""The exponents the registry supports. Both are computable with sqrt alone.

The TypeScript side expresses this as the union type ``1 | 0.75``, so a third
exponent cannot be added without confronting the reproducibility question.
Python's ``Literal`` does not accept floats, so the constraint is enforced at
construction instead.
"""


def zipf_weight(rank: int, alpha: float) -> float:
    """Weight of one rank, without calling ``pow``.

    Args:
        rank: zero-based; rank 0 is the most popular key.
        alpha: 1.0 or 0.75.
    """
    r = rank + 1
    if alpha == 1.0:
        return 1 / r
    # r**0.75 = sqrt(r) * sqrt(sqrt(r)) — two correctly rounded operations.
    s = math.sqrt(r)
    q = math.sqrt(s)
    return 1 / (s * q)


class ZipfSampler:
    """A precomputed Zipf distribution over ranks ``0 .. size - 1``.

    The sampled rank *is* the key, so key 0 is the most popular. Sampling is
    inverse-CDF by binary search and consumes exactly one ``next_float()``.
    """

    __slots__ = ("_cumulative", "_total", "alpha", "size")

    def __init__(self, size: int, alpha: float) -> None:
        if not isinstance(size, int) or isinstance(size, bool) or size < 1:
            msg = f"ZipfSampler: size must be a positive integer, received {size!r}"
            raise ValueError(msg)
        if alpha not in SUPPORTED_ALPHAS:
            msg = (
                f"ZipfSampler: alpha must be one of {SUPPORTED_ALPHAS}, received {alpha!r}. "
                "Other exponents would need pow, which is not correctly rounded across "
                "C standard libraries and would break trace reproducibility."
            )
            raise ValueError(msg)

        self.size = size
        self.alpha = alpha

        # Summed in ascending rank order, which fixes the floating-point result.
        # Any other order gives a slightly different total.
        cumulative = [0.0] * size
        running = 0.0
        for rank in range(size):
            running += zipf_weight(rank, alpha)
            cumulative[rank] = running

        self._cumulative = cumulative
        self._total = running

    def sample(self, rng: Rng) -> int:
        """Draw a rank, consuming exactly one ``next_float()``."""
        target = rng.next_float() * self._total
        cumulative = self._cumulative

        low = 0
        high = self.size - 1
        while low < high:
            mid = (low + high) >> 1
            if cumulative[mid] > target:
                high = mid
            else:
                low = mid + 1
        return low

"""Shared metric helpers.

Benchmark numbers are committed and compared across languages, so rounding has
to be specified rather than assumed.
"""

from __future__ import annotations

import math

__all__ = ["round6"]


def round6(value: float) -> float:
    """Round to six decimal places, half away from zero.

    Deliberately **not** Python's built-in ``round``, which uses banker's
    rounding: ``round(0.0000005, 6)`` gives 0.0 while JavaScript's
    ``Math.round(0.5) / 1e6`` gives 1e-06. The registry's reference
    implementation is JavaScript, so Python matches it with an explicit
    floor-of-plus-a-half.

    All registry metrics are non-negative, so there is no negative-tie case.
    """
    if not math.isfinite(value):
        return value
    return math.floor(value * 1e6 + 0.5) / 1e6

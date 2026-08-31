"""EqualJitter — half the exponential delay fixed, half of it random.

The middle ground between plain exponential backoff, which spreads nothing, and
full jitter, which spreads everything and halves the expected wait in the
process::

    half  = min(cap, base * 2 ** (attempt - 1)) // 2
    delay = half + rng.next_int(half + 1)

A delay always lands in ``[half, 2 * half]`` and averages about three quarters
of the un-jittered ceiling, against full jitter's one half. Choose it when the
backoff must actually back off: full jitter can return zero on any attempt, and
this cannot fall below half the ceiling.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

from policybook.domains.retry.interface import RetryError
from policybook.rng import Rng

DEFAULT_BASE_MS = 100
DEFAULT_CAP_MS = 10_000
DEFAULT_MAX_ATTEMPTS = 8


def _backoff_ceiling(attempt: int, base_ms: int, cap_ms: int) -> int:
    """``min(cap, base * 2 ** (attempt - 1))``, without overflowing.

    Restated in each policy rather than imported from a sibling, because
    ``policybook add`` copies a policy file whole and a copy that reached back
    into the registry would not run in the reader's project.
    """
    delay = base_ms
    for _ in range(1, attempt):
        if delay >= cap_ms:
            return cap_ms
        delay *= 2
    return min(delay, cap_ms)


class EqualJitter:
    """Exponential backoff with half the delay drawn and half guaranteed."""

    __slots__ = ("_base_ms", "_cap_ms", "_max_attempts", "_rng")

    def __init__(
        self,
        base_ms: int = DEFAULT_BASE_MS,
        cap_ms: int = DEFAULT_CAP_MS,
        max_attempts: int = DEFAULT_MAX_ATTEMPTS,
        rng: Rng | None = None,
    ) -> None:
        if not isinstance(base_ms, int) or isinstance(base_ms, bool) or base_ms < 1:
            msg = f"EqualJitter: base_ms must be a positive integer, received {base_ms!r}"
            raise ValueError(msg)
        if not isinstance(cap_ms, int) or isinstance(cap_ms, bool) or cap_ms < 1:
            msg = f"EqualJitter: cap_ms must be a positive integer, received {cap_ms!r}"
            raise ValueError(msg)
        if (
            not isinstance(max_attempts, int)
            or isinstance(max_attempts, bool)
            or max_attempts < 1
        ):
            msg = (
                f"EqualJitter: max_attempts must be a positive integer, received {max_attempts!r}"
            )
            raise ValueError(msg)

        self._base_ms = base_ms
        self._cap_ms = cap_ms
        self._max_attempts = max_attempts
        self._rng = Rng(0) if rng is None else rng

    def next_delay(self, attempt: int, error: RetryError) -> int | None:
        """How long to wait before the next attempt, or None to give up."""
        if not error.get("retryable", False):
            return None
        if attempt >= self._max_attempts:
            return None

        # Integer halving. At a ceiling of 1 the half is 0 and every delay is 0
        # — degenerate, but that is what the formula says, and a base of one
        # millisecond is not a configuration to rely on.
        half = _backoff_ceiling(attempt, self._base_ms, self._cap_ms) // 2
        return half + self._rng.next_int(half + 1)

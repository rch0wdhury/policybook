# GENERATED COPY — do not edit. Edit policies/retry/decorrelated-jitter/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""DecorrelatedJitter — grow the delay from the last delay, not the attempt number.

The other policies in this domain compute their delay from ``attempt``, so a
client's whole schedule is determined the moment it starts failing. This one is a
random walk instead::

    delay = min(cap, base + rng.next_int(prev * 3 - base + 1))
    prev  = delay

``prev`` is state, and it is the whole idea: the delay depends on the history of
the retry sequence rather than on its length, so whole schedules diverge rather
than individual attempts.

It climbs more slowly than doubling despite reaching for three times the last
delay — the draw is uniform over that range, so the expected step is about 1.5x
against exponential's exact 2x. The variance is the point, not the speed.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

from policybook.domains.retry.interface import RetryError
from policybook.rng import Rng

DEFAULT_BASE_MS = 100
DEFAULT_CAP_MS = 10_000
DEFAULT_MAX_ATTEMPTS = 8


class DecorrelatedJitter:
    """A random walk whose next range depends on its last step."""

    __slots__ = ("_base_ms", "_cap_ms", "_max_attempts", "_previous_ms", "_rng")

    def __init__(
        self,
        base_ms: int = DEFAULT_BASE_MS,
        cap_ms: int = DEFAULT_CAP_MS,
        max_attempts: int = DEFAULT_MAX_ATTEMPTS,
        rng: Rng | None = None,
    ) -> None:
        if not isinstance(base_ms, int) or isinstance(base_ms, bool) or base_ms < 1:
            msg = (
                f"DecorrelatedJitter: base_ms must be a positive integer, received {base_ms!r}"
            )
            raise ValueError(msg)
        if not isinstance(cap_ms, int) or isinstance(cap_ms, bool) or cap_ms < 1:
            msg = f"DecorrelatedJitter: cap_ms must be a positive integer, received {cap_ms!r}"
            raise ValueError(msg)
        if (
            not isinstance(max_attempts, int)
            or isinstance(max_attempts, bool)
            or max_attempts < 1
        ):
            msg = (
                "DecorrelatedJitter: max_attempts must be a positive integer, "
                f"received {max_attempts!r}"
            )
            raise ValueError(msg)

        self._base_ms = base_ms
        self._cap_ms = cap_ms
        self._max_attempts = max_attempts
        self._rng = Rng(0) if rng is None else rng
        # Where the walk begins.
        self._previous_ms = base_ms

    def next_delay(self, attempt: int, error: RetryError) -> int | None:
        """How long to wait before the next attempt, or None to give up."""
        if not error.get("retryable", False):
            return None
        if attempt >= self._max_attempts:
            return None

        # ``prev * 3 - base`` is always positive: ``prev`` starts at ``base`` and
        # every delay is at least ``base``, so the smallest span is ``2 * base``.
        span = self._previous_ms * 3 - self._base_ms
        delay = min(self._cap_ms, self._base_ms + self._rng.next_int(span + 1))

        # The walk advances from the delay actually used, cap included —
        # otherwise a client at the cap would keep drawing from an ever-growing
        # range it can never reach.
        self._previous_ms = delay
        return delay

    def previous_delay(self) -> int:
        """The delay last returned. Not part of the interface; used by tests."""
        return self._previous_ms

# GENERATED COPY — do not edit. Edit policies/retry/exponential/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""Exponential — double the wait after every failure, up to a cap.

The textbook answer, and a genuine improvement on constant backoff: load on a
struggling service falls off geometrically as an outage continues.

It still synchronises clients, and that is the reason not to use it. The delay
is a pure function of the attempt number, so every client that failed at the
same moment retries at the same moment, every time. Backing off exponentially
converts a continuous herd into a periodic one; it does not disperse it.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

from policybook.domains.retry.interface import RetryError
from policybook.rng import Rng

DEFAULT_BASE_MS = 100
DEFAULT_CAP_MS = 10_000
DEFAULT_MAX_ATTEMPTS = 8


def backoff_ceiling(attempt: int, base_ms: int, cap_ms: int) -> int:
    """``min(cap, base * 2 ** (attempt - 1))``, without overflowing.

    Shared with the jittered variants, which differ only in what they do with
    this number. Doubling stops as soon as the cap is reached, so the loop does
    a bounded number of multiplications however large ``attempt`` is — which
    matters far more in the C and TypeScript ports than it does here, but the
    three must agree.
    """
    delay = base_ms
    for _ in range(1, attempt):
        if delay >= cap_ms:
            return cap_ms
        delay *= 2
    return min(delay, cap_ms)


class Exponential:
    """A delay that doubles after every failure."""

    __slots__ = ("_base_ms", "_cap_ms", "_max_attempts")

    def __init__(
        self,
        base_ms: int = DEFAULT_BASE_MS,
        cap_ms: int = DEFAULT_CAP_MS,
        max_attempts: int = DEFAULT_MAX_ATTEMPTS,
        rng: Rng | None = None,
    ) -> None:
        # Accepted and ignored: no draw anywhere, which is why it synchronises.
        del rng

        if not isinstance(base_ms, int) or isinstance(base_ms, bool) or base_ms < 1:
            msg = f"Exponential: base_ms must be a positive integer, received {base_ms!r}"
            raise ValueError(msg)
        if not isinstance(cap_ms, int) or isinstance(cap_ms, bool) or cap_ms < 1:
            msg = f"Exponential: cap_ms must be a positive integer, received {cap_ms!r}"
            raise ValueError(msg)
        if (
            not isinstance(max_attempts, int)
            or isinstance(max_attempts, bool)
            or max_attempts < 1
        ):
            msg = (
                "Exponential: max_attempts must be a positive integer, "
                f"received {max_attempts!r}"
            )
            raise ValueError(msg)

        self._base_ms = base_ms
        self._cap_ms = cap_ms
        self._max_attempts = max_attempts

    def next_delay(self, attempt: int, error: RetryError) -> int | None:
        """How long to wait before the next attempt, or None to give up."""
        if not error.get("retryable", False):
            return None
        if attempt >= self._max_attempts:
            return None
        return backoff_ceiling(attempt, self._base_ms, self._cap_ms)

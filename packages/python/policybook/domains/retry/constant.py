# GENERATED COPY — do not edit. Edit policies/retry/constant/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""Constant — wait the same amount every time.

The baseline, and the policy you get by accident when nobody thought about it:
``sleep(100)`` in a loop is this.

It has one real virtue — it is the fastest to notice a *short* outage — and one
serious vice: the load it puts on a struggling service does not fall as the
outage continues, and with no randomness anywhere, every client that failed
together comes back together, forever.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

from policybook.domains.retry.interface import RetryError
from policybook.rng import Rng

DEFAULT_BASE_MS = 100
DEFAULT_MAX_ATTEMPTS = 8


class Constant:
    """The same delay before every retry."""

    __slots__ = ("_base_ms", "_max_attempts")

    def __init__(
        self,
        base_ms: int = DEFAULT_BASE_MS,
        max_attempts: int = DEFAULT_MAX_ATTEMPTS,
        rng: Rng | None = None,
    ) -> None:
        # `rng` is accepted and ignored: every policy in the registry is
        # constructed the same way, and this one drawing nothing from it is
        # precisely its defining weakness.
        del rng

        if not isinstance(base_ms, int) or isinstance(base_ms, bool) or base_ms < 0:
            msg = f"Constant: base_ms must be a non-negative integer, received {base_ms!r}"
            raise ValueError(msg)
        if (
            not isinstance(max_attempts, int)
            or isinstance(max_attempts, bool)
            or max_attempts < 1
        ):
            msg = f"Constant: max_attempts must be a positive integer, received {max_attempts!r}"
            raise ValueError(msg)

        self._base_ms = base_ms
        self._max_attempts = max_attempts

    def next_delay(self, attempt: int, error: RetryError) -> int | None:
        """How long to wait before the next attempt, or None to give up."""
        # Nothing is gained by retrying a failure the server says is permanent.
        if not error.get("retryable", False):
            return None
        if attempt >= self._max_attempts:
            return None
        return self._base_ms

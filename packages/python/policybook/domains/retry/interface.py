"""The ``retry`` domain interface.

Mirrors ``packages/core/src/domains/retry/interface.ts`` method for method;
only the spelling is idiomatic Python (concept.md §12.3).

A request failed; when should the client come back? Retry too eagerly and a
service that was merely slow becomes a service that is down, because every
client in the fleet is now hammering it in lockstep. The interesting part is not
the delay curve but the randomness: without jitter, every client that saw the
same failure retries at the same instant.

A policy is handed an ``Rng`` at construction rather than owning one, and rather
than receiving one per call as concept.md §5.1 shows. Every other domain in this
registry supplies randomness at construction — the C vtable's
``create(params, allocator, rng)`` already does — and making retry the exception
would put a ``pb_rng *`` on the C hot path for no benefit.
"""

from __future__ import annotations

from typing import Final, Protocol, TypedDict

__all__ = ["RETRY_REFERENCE", "RetryError", "RetryPolicy"]


class RetryError(TypedDict, total=False):
    """What went wrong, as much of it as a policy is allowed to know."""

    status: int
    """HTTP-style status, when there is one."""

    retryable: bool
    """Whether retrying could plausibly help at all."""

    retry_after_ms: int
    """How long the server asked the client to wait, in milliseconds.

    A harness extension rather than part of the original interface, and optional:
    most errors do not carry one. ``Retry-After`` is the server's own estimate of
    when it will be ready, and a policy that ignores it is guessing when it has
    been told.
    """


class RetryPolicy(Protocol):
    """Every retry policy implements this, and nothing more."""

    def next_delay(self, attempt: int, error: RetryError) -> int | None:
        """How long to wait before attempt ``attempt + 1``, or None to give up.

        ``attempt`` is 1-based: it is the number of the attempt that just
        failed, so the first call always has ``attempt == 1``. Returning None is
        a decision, not an error — it means this policy believes further
        attempts are not worth making.
        """
        ...


RETRY_REFERENCE: Final[dict[str, int]] = {
    "base_ms": 100,
    "cap_ms": 10_000,
    "max_attempts": 8,
}
"""The reference configuration every canonical benchmark uses."""

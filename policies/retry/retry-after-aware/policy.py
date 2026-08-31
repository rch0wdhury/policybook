"""RetryAfterAware — do what the server asked, and guess only when it did not.

Every other policy in this domain is guessing. This one reads the answer when the
server has provided it: a ``Retry-After`` header is the service's own statement
of when it expects to be ready. When the header is absent it falls back to full
jitter, so it is never worse than the default::

    if the error carries a Retry-After:  delay = min(cap, retry_after_ms)
    otherwise:                           delay = rng.next_int(ceiling + 1)

The clamp is not a formality: a client that honours an arbitrary hint has handed
a stranger control of its own latency budget.

This policy re-synchronises clients by construction — a thousand told to come
back in five seconds all come back in five seconds — which is the herd jitter
exists to break. See ``README.md`` for when that trade is worth making.

Mirrors ``index.ts``.
"""

from __future__ import annotations

from policybook.domains.retry.interface import RetryError
from policybook.rng import Rng

DEFAULT_BASE_MS = 100
DEFAULT_CAP_MS = 10_000
DEFAULT_MAX_ATTEMPTS = 8


def _backoff_ceiling(attempt: int, base_ms: int, cap_ms: int) -> int:
    """``min(cap, base * 2 ** (attempt - 1))``, without overflowing.

    Restated rather than imported from a sibling policy, because
    ``policybook add`` copies a policy file whole.
    """
    delay = base_ms
    for _ in range(1, attempt):
        if delay >= cap_ms:
            return cap_ms
        delay *= 2
    return min(delay, cap_ms)


class RetryAfterAware:
    """Honour the server's estimate, clamped; fall back to full jitter."""

    __slots__ = ("_base_ms", "_cap_ms", "_max_attempts", "_rng")

    def __init__(
        self,
        base_ms: int = DEFAULT_BASE_MS,
        cap_ms: int = DEFAULT_CAP_MS,
        max_attempts: int = DEFAULT_MAX_ATTEMPTS,
        rng: Rng | None = None,
    ) -> None:
        if not isinstance(base_ms, int) or isinstance(base_ms, bool) or base_ms < 1:
            msg = f"RetryAfterAware: base_ms must be a positive integer, received {base_ms!r}"
            raise ValueError(msg)
        if not isinstance(cap_ms, int) or isinstance(cap_ms, bool) or cap_ms < 1:
            msg = f"RetryAfterAware: cap_ms must be a positive integer, received {cap_ms!r}"
            raise ValueError(msg)
        if (
            not isinstance(max_attempts, int)
            or isinstance(max_attempts, bool)
            or max_attempts < 1
        ):
            msg = (
                "RetryAfterAware: max_attempts must be a positive integer, "
                f"received {max_attempts!r}"
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

        hint = error.get("retry_after_ms")
        if hint is not None:
            # A hint of zero is a real instruction — come back now — and is
            # honoured. Absent and zero are different statements, which is why
            # the check is for None rather than for falsiness.
            if not isinstance(hint, int) or isinstance(hint, bool) or hint < 0:
                msg = (
                    "RetryAfterAware: retry_after_ms must be a non-negative integer, "
                    f"received {hint!r}"
                )
                raise ValueError(msg)
            # No draw is consumed on this path. A port that drew anyway would
            # leave its stream in a different place and diverge on the next
            # fallback.
            return min(self._cap_ms, hint)

        return self._rng.next_int(
            _backoff_ceiling(attempt, self._base_ms, self._cap_ms) + 1
        )

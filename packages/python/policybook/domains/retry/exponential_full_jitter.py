# GENERATED COPY — do not edit. Edit policies/retry/exponential-full-jitter/policy.py instead,
# then run: pnpm tsx scripts/assemble-python.ts

"""ExponentialFullJitter — a uniform delay between zero and the exponential ceiling.

The default worth shipping. It keeps everything exponential backoff gets right —
load falls geometrically as an outage continues — and fixes the thing it gets
wrong: clients no longer retry in lockstep, because each picks its own delay.

The expected delay halves, since a uniform draw from ``[0, c]`` averages ``c/2``,
so this is more aggressive than plain exponential rather than less. It comes out
ahead anyway because the spreading matters more than the average.

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

    The same function ``Exponential`` exports. It is restated here rather than
    imported because a policy file is copied out of the registry whole by
    ``policybook add``, and a copy that reached back into a sibling policy would
    not compile in the reader's project.
    """
    delay = base_ms
    for _ in range(1, attempt):
        if delay >= cap_ms:
            return cap_ms
        delay *= 2
    return min(delay, cap_ms)


class ExponentialFullJitter:
    """Exponential backoff, drawn uniformly rather than taken at the ceiling."""

    __slots__ = ("_base_ms", "_cap_ms", "_max_attempts", "_rng")

    def __init__(
        self,
        base_ms: int = DEFAULT_BASE_MS,
        cap_ms: int = DEFAULT_CAP_MS,
        max_attempts: int = DEFAULT_MAX_ATTEMPTS,
        rng: Rng | None = None,
    ) -> None:
        if not isinstance(base_ms, int) or isinstance(base_ms, bool) or base_ms < 1:
            msg = (
                "ExponentialFullJitter: base_ms must be a positive integer, "
                f"received {base_ms!r}"
            )
            raise ValueError(msg)
        if not isinstance(cap_ms, int) or isinstance(cap_ms, bool) or cap_ms < 1:
            msg = (
                f"ExponentialFullJitter: cap_ms must be a positive integer, received {cap_ms!r}"
            )
            raise ValueError(msg)
        if (
            not isinstance(max_attempts, int)
            or isinstance(max_attempts, bool)
            or max_attempts < 1
        ):
            msg = (
                "ExponentialFullJitter: max_attempts must be a positive integer, "
                f"received {max_attempts!r}"
            )
            raise ValueError(msg)

        self._base_ms = base_ms
        self._cap_ms = cap_ms
        self._max_attempts = max_attempts
        # Seeded rather than left absent: a policy constructed without one still
        # has to produce a delay, and an unseeded default would be a global
        # source by another name.
        self._rng = Rng(0) if rng is None else rng

    def next_delay(self, attempt: int, error: RetryError) -> int | None:
        """How long to wait before the next attempt, or None to give up.

        The two refusals come before the draw, so a call this policy declines
        leaves its random stream exactly where it was.
        """
        if not error.get("retryable", False):
            return None
        if attempt >= self._max_attempts:
            return None

        # ``next_int(n)`` returns 0..n-1, so the bound is the ceiling plus one
        # and the ceiling itself remains reachable. Zero is reachable too, and
        # that is deliberate: some client retrying immediately is what makes the
        # arrival pattern smooth rather than merely delayed.
        ceiling = _backoff_ceiling(attempt, self._base_ms, self._cap_ms)
        return self._rng.next_int(ceiling + 1)

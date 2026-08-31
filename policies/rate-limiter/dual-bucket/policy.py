"""DualBucket — two limits at once, and a request must satisfy both.

The shape every LLM API uses: a requests-per-minute ceiling and a
tokens-per-minute ceiling, checked together. One dimension counts calls, the
other counts how much work each call asks for, and either can refuse on its own.

Each dimension is a token bucket with a per-minute period. The charge is atomic:
if either dimension would refuse, neither is charged, so a caller refused for
work has not quietly spent a request too.

Mirrors ``index.ts``; see ``README.md`` for when this is and is not the right
policy.
"""

from __future__ import annotations

DEFAULT_REQUESTS_PER_MIN = 500
DEFAULT_TOKENS_PER_MIN = 200_000

PERIOD_MS = 60_000
"""Both ceilings are stated per minute, so the ledger's period is a minute."""


class DualBucket:
    """Two per-minute ledgers per key, charged together."""

    __slots__ = (
        "_last",
        "_request_credit",
        "_requests",
        "_requests_per_min",
        "_token_credit",
        "_tokens",
        "_tokens_per_min",
    )

    def __init__(
        self,
        requests_per_min: int = DEFAULT_REQUESTS_PER_MIN,
        tokens_per_min: int = DEFAULT_TOKENS_PER_MIN,
    ) -> None:
        if (
            not isinstance(requests_per_min, int)
            or isinstance(requests_per_min, bool)
            or requests_per_min < 1
        ):
            msg = (
                "DualBucket: requests_per_min must be a positive integer, "
                f"received {requests_per_min!r}"
            )
            raise ValueError(msg)
        if (
            not isinstance(tokens_per_min, int)
            or isinstance(tokens_per_min, bool)
            or tokens_per_min < 1
        ):
            msg = (
                "DualBucket: tokens_per_min must be a positive integer, "
                f"received {tokens_per_min!r}"
            )
            raise ValueError(msg)

        self._requests_per_min = requests_per_min
        self._tokens_per_min = tokens_per_min

        self._requests: dict[int, int] = {}
        self._request_credit: dict[int, int] = {}
        self._tokens: dict[int, int] = {}
        self._token_credit: dict[int, int] = {}
        self._last: dict[int, int] = {}

    def _refill(self, key: int, now: int) -> None:
        """Bring both ledgers up to date at ``now``.

        The token bucket's integer ledger with a period of a minute instead of a
        second: the fraction lives in a credit accumulator measured in
        ``PERIOD_MS``ths of a permit, so nothing is rounded away. Elapsed time is
        clamped to one period, which is exactly how long a drained bucket takes
        to refill.
        """
        elapsed = now - self._last[key]
        if elapsed <= 0:
            return
        elapsed = min(elapsed, PERIOD_MS)

        credit = self._request_credit[key] + self._requests_per_min * elapsed
        requests = self._requests[key] + credit // PERIOD_MS
        credit %= PERIOD_MS
        if requests >= self._requests_per_min:
            requests = self._requests_per_min
            credit = 0
        self._requests[key] = requests
        self._request_credit[key] = credit

        credit = self._token_credit[key] + self._tokens_per_min * elapsed
        tokens = self._tokens[key] + credit // PERIOD_MS
        credit %= PERIOD_MS
        if tokens >= self._tokens_per_min:
            tokens = self._tokens_per_min
            credit = 0
        self._tokens[key] = tokens
        self._token_credit[key] = credit

        self._last[key] = now

    def _bucket_for(self, key: int, now: int) -> None:
        if key not in self._requests:
            self._requests[key] = self._requests_per_min
            self._request_credit[key] = 0
            self._tokens[key] = self._tokens_per_min
            self._token_credit[key] = 0
            self._last[key] = now
            return
        self._refill(key, now)

    def allow(self, key: int, cost: int, now: int) -> bool:
        """May a call costing ``cost`` units of work proceed?

        The call always charges one request; ``cost`` is what it charges the
        work dimension. Both must be affordable, and both are charged or
        neither is.
        """
        self._bucket_for(key, now)

        if self._requests[key] < 1:
            return False
        if self._tokens[key] < cost:
            return False

        self._requests[key] -= 1
        self._tokens[key] -= cost
        return True

    def retry_after(self, key: int, now: int) -> int:
        """Milliseconds until a smallest possible call would be admitted.

        The later of the two dimensions, because a caller has to satisfy both.
        It cannot account for the size of the call you actually intend to make:
        the interface has no cost argument, so a large call may still be refused
        after this elapses.
        """
        if key not in self._requests:
            return 0

        self._refill(key, now)
        return max(
            _wait_for(self._requests[key], self._request_credit[key], self._requests_per_min),
            _wait_for(self._tokens[key], self._token_credit[key], self._tokens_per_min),
        )

    def state_size(self) -> int:
        """How many keys are tracked. Five integers each."""
        return len(self._requests)

    def requests_of(self, key: int, now: int) -> int:
        """Requests left this minute. Not part of the interface; used by tests."""
        if key not in self._requests:
            return self._requests_per_min
        self._refill(key, now)
        return self._requests[key]

    def tokens_of(self, key: int, now: int) -> int:
        """Work units left this minute. Not part of the interface; used by tests."""
        if key not in self._tokens:
            return self._tokens_per_min
        self._refill(key, now)
        return self._tokens[key]


def _wait_for(available: int, credit: int, rate_per_min: int) -> int:
    """Milliseconds until one whole permit accrues on a dimension."""
    if available >= 1:
        return 0
    return -(-(PERIOD_MS - credit) // rate_per_min)

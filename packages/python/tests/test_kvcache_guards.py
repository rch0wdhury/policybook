"""The kv-cache over-capacity guard, tripped at exactly the reference's step.

A policy holds at most ``budget + 1`` positions — the instant between a decode
step and the evict that follows it. The TypeScript ports throw on the step
that would exceed that; a Python port that silently kept growing would accept
traces the reference rejects, which is the one divergence the shared vectors
cannot see because the harness always evicts.
"""

from __future__ import annotations

from collections.abc import Callable, Sequence
from typing import Protocol

import pytest

from policybook.domains.kv_cache import H2o, PyramidKv, Scissorhands, SnapKv, Tova

BUDGET = 4


class _GrowingPolicy(Protocol):
    """The slice of the domain interface this test drives."""

    def on_decode_step(self, pos: int, attn: Sequence[float] | None) -> None: ...

    def kept_count(self) -> int: ...


CASES: list[tuple[str, Callable[[], _GrowingPolicy], str]] = [
    (
        "h2o",
        lambda: H2o(budget=BUDGET, recent_window=2),
        "H2o: asked to hold 6 positions with a budget of 4",
    ),
    (
        "tova",
        lambda: Tova(budget=BUDGET),
        "Tova: asked to hold 6 positions with a budget of 4",
    ),
    (
        "scissorhands",
        lambda: Scissorhands(budget=BUDGET, recent_window=2),
        "Scissorhands: asked to hold 6 positions with a budget of 4",
    ),
    (
        "snapkv",
        lambda: SnapKv(budget=BUDGET, recent_window=2, obs_window=2, pool_kernel=3),
        "SnapKv: asked to hold 6 positions with a budget of 4",
    ),
    (
        "pyramidkv",
        lambda: PyramidKv(
            budget=BUDGET,
            layer=0,
            num_layers=1,
            pyramid_ratio=4,
            recent_window=2,
            obs_window=2,
            pool_kernel=3,
        ),
        "PyramidKv: asked to hold 6 positions with room for 4",
    ),
]


@pytest.mark.parametrize(
    ("build", "message"),
    [pytest.param(build, message, id=name) for name, build, message in CASES],
)
def test_step_past_capacity_raises_where_the_reference_throws(
    build: Callable[[], _GrowingPolicy],
    message: str,
) -> None:
    policy = build()

    # Position 0 is held from construction, and each step adds one, so after
    # `budget` steps the kept set stands at budget + 1 — allowed, since the
    # harness evicts between steps. The step after that is the one the
    # reference refuses.
    for pos in range(1, BUDGET + 1):
        policy.on_decode_step(pos, None)
    assert policy.kept_count() == BUDGET + 1

    with pytest.raises(RuntimeError, match=message):
        policy.on_decode_step(BUDGET + 1, None)

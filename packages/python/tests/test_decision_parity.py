"""The Python cache ports must reproduce the committed decision streams.

The shared vectors pin small hand-authored scenarios and the trace-prefix
files pin the generators, but neither replays a policy's *decisions* over a
whole trace across languages. That gap is exactly where the 2Q/S3-FIFO ghost
divergence lived: three ports, all green, disagreeing on every canonical
trace. The reference hashes are generated from the TypeScript ports by
``scripts/gen-decision-parity.ts`` and committed; this suite recomputes them
with the Python ports, byte for byte.

Protocol (identical in all three languages): FNV-1a64 over one outcome byte
per event (1 hit, 0 miss inserted, 2 miss rejected by ``admit``), and each
victim key as four little-endian bytes.
"""

from __future__ import annotations

import json
from collections.abc import Callable
from pathlib import Path
from typing import Any, Protocol

import pytest
from _repo import find_repo_root

from policybook.cache import (
    CACHE_TRACES,
    Arc,
    Clock,
    Fifo,
    Lfu,
    Lru,
    Opt,
    S3Fifo,
    Sieve,
    TwoQueue,
    WTinyLfu,
    generate_cache_trace,
)

REPO_ROOT = find_repo_root(Path(__file__).parent)
ARTIFACT = REPO_ROOT / "packages" / "core" / "src" / "domains" / "cache" / "decision-parity.json"

FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
FNV_MASK = 0xFFFFFFFFFFFFFFFF


class DrivenPolicy(Protocol):
    def on_access(self, key: int, hit: bool, meta: dict[str, Any] | None = None) -> None: ...

    def evict(self) -> int: ...


# Mirrors CHURN_STREAMS in packages/core/src/domains/cache/decision-parity.ts.
CHURN_STREAMS = (
    {"id": "churn-small", "seed": 1, "capacity": 4, "key_universe": 10, "events": 20_000},
    {"id": "churn-wide", "seed": 4, "capacity": 10, "key_universe": 30, "events": 20_000},
)

FACTORIES: dict[str, Callable[[int, list[int]], DrivenPolicy]] = {
    "cache/2q": lambda capacity, future: TwoQueue(capacity=capacity),
    "cache/arc": lambda capacity, future: Arc(capacity=capacity),
    "cache/clock": lambda capacity, future: Clock(capacity=capacity),
    "cache/fifo": lambda capacity, future: Fifo(capacity=capacity),
    "cache/lfu": lambda capacity, future: Lfu(capacity=capacity),
    "cache/lru": lambda capacity, future: Lru(capacity=capacity),
    "cache/opt": lambda capacity, future: Opt(capacity=capacity, future=future),
    "cache/s3-fifo": lambda capacity, future: S3Fifo(capacity=capacity),
    "cache/sieve": lambda capacity, future: Sieve(capacity=capacity),
    "cache/w-tinylfu": lambda capacity, future: WTinyLfu(capacity=capacity),
}


def generate_churn_stream(spec: dict[str, int]) -> list[int]:
    state = spec["seed"] & 0xFFFFFFFF
    out: list[int] = []
    for _ in range(spec["events"]):
        state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
        out.append(state % spec["key_universe"])
    return out


def drive(
    policy: DrivenPolicy, trace: list[int], capacity: int, key_universe: int
) -> dict[str, Any]:
    resident = bytearray(key_universe)
    resident_count = 0
    hits = 0
    evictions = 0
    rejections = 0
    digest = FNV_OFFSET

    admit = getattr(policy, "admit", None)

    for key in trace:
        hit = resident[key] == 1
        policy.on_access(key, hit)

        if hit:
            hits += 1
            digest = ((digest ^ 1) * FNV_PRIME) & FNV_MASK
            continue

        if admit is not None and not admit(key):
            rejections += 1
            digest = ((digest ^ 2) * FNV_PRIME) & FNV_MASK
            continue
        digest = (digest * FNV_PRIME) & FNV_MASK  # ^ 0 is a no-op

        resident[key] = 1
        resident_count += 1
        while resident_count > capacity:
            victim = policy.evict()
            assert resident[victim] == 1, f"evicted non-resident {victim}"
            resident[victim] = 0
            resident_count -= 1
            evictions += 1
            digest = ((digest ^ (victim & 0xFF)) * FNV_PRIME) & FNV_MASK
            digest = ((digest ^ ((victim >> 8) & 0xFF)) * FNV_PRIME) & FNV_MASK
            digest = ((digest ^ ((victim >> 16) & 0xFF)) * FNV_PRIME) & FNV_MASK
            digest = ((digest ^ ((victim >> 24) & 0xFF)) * FNV_PRIME) & FNV_MASK

    return {
        "hash": format(digest, "016x"),
        "hits": hits,
        "evictions": evictions,
        "rejections": rejections,
    }


@pytest.fixture(scope="session")
def reference() -> dict[str, Any]:
    data: dict[str, Any] = json.loads(ARTIFACT.read_text(encoding="utf-8"))
    return data["policies"]


def test_every_policy_is_recorded(reference: dict[str, Any]) -> None:
    assert sorted(reference) == sorted(FACTORIES)


@pytest.mark.parametrize("policy_id", sorted(FACTORIES))
def test_decision_parity(policy_id: str, reference: dict[str, Any]) -> None:
    expected = reference[policy_id]
    factory = FACTORIES[policy_id]

    for trace_id, spec in CACHE_TRACES.items():
        trace = generate_cache_trace(trace_id)
        policy = factory(spec.capacity, trace)
        actual = drive(policy, trace, spec.capacity, spec.key_universe)
        assert actual == expected[trace_id], f"{policy_id} diverges on {trace_id}"

    for stream in CHURN_STREAMS:
        trace = generate_churn_stream(stream)
        policy = factory(stream["capacity"], trace)
        actual = drive(policy, trace, stream["capacity"], stream["key_universe"])
        assert actual == expected[stream["id"]], f"{policy_id} diverges on {stream['id']}"

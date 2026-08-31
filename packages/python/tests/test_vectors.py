"""The Python vector runner, and every Python policy in the registry.

Two things live here. The first is a unit test of the runner itself, against a
stand-in policy — the runner is what proves every future port correct, so it
needs proving first. The second is the registry sweep: every policy declaring a
``python`` port is replayed against the same ``vectors.json`` the TypeScript and
C implementations use. The catalog is empty today and fills from T10.
"""

from __future__ import annotations

import importlib
import json
import math
from array import array
from pathlib import Path
from typing import Any

import pytest
from _repo import find_repo_root

from policybook._vectors import (
    camel_to_snake,
    compare_values,
    load_policy_class,
    run_vectors,
)
from policybook.rng import Rng

REPO_ROOT = find_repo_root(Path(__file__).parent)


class FakePolicy:
    """A stand-in with one method of each interesting shape."""

    def __init__(self, capacity: int = 10, rng: Rng | None = None) -> None:
        self.capacity = capacity
        self.rng = rng
        self.keys: list[str] = []

    def on_access(self, key: str, hit: bool = False) -> None:
        if not hit:
            self.keys.append(key)

    def evict(self) -> str:
        return self.keys.pop(0) if self.keys else ""

    def size(self) -> int:
        return len(self.keys)

    def configured_capacity(self) -> int:
        return self.capacity

    def ratio(self) -> float:
        return 1 / 3

    def positions(self) -> list[int]:
        return [1, 2, 3]

    def maybe(self) -> int | None:
        return None

    def draw(self) -> int:
        assert self.rng is not None
        return self.rng.next_u32()

    def explode(self) -> None:
        msg = "boom"
        raise RuntimeError(msg)


class NoRngPolicy:
    """A deterministic policy that does not declare an ``rng`` parameter."""

    def __init__(self, capacity: int = 4) -> None:
        self.capacity = capacity

    def configured_capacity(self) -> int:
        return self.capacity


def vectors_file(cases: list[dict[str, Any]]) -> dict[str, Any]:
    return {"policy": "fake/policy", "version": 1, "cases": cases}


# --- the camelCase to snake_case mapping ------------------------------------


@pytest.mark.parametrize(
    ("reference", "python"),
    [
        ("onAccess", "on_access"),
        ("evict", "evict"),
        ("nextDelay", "next_delay"),
        ("onDecodeStep", "on_decode_step"),
        ("retryAfter", "retry_after"),
        ("stateSize", "state_size"),
        ("cwnd", "cwnd"),
    ],
)
def test_camel_to_snake(reference: str, python: str) -> None:
    assert camel_to_snake(reference) == python


# --- the comparison rules ----------------------------------------------------


def test_compare_accepts_equal_values() -> None:
    assert compare_values(1, 1) is None
    assert compare_values("a", "a") is None
    assert compare_values(True, True) is None
    assert compare_values(None, None) is None
    assert compare_values([1, 2], [1, 2]) is None
    assert compare_values({"a": 1}, {"a": 1}) is None


def test_compare_does_not_let_a_bool_satisfy_a_number() -> None:
    # In Python ``True == 1``, so a naive numeric comparison would accept a
    # bool where a vector asked for 1 — and, worse, the other way around.
    assert compare_values(True, 1) is not None
    assert compare_values(1, True) is not None
    assert compare_values(False, 0) is not None
    assert compare_values(0, False) is not None


def test_compare_honours_tolerance() -> None:
    assert compare_values(0.3333333333, 1 / 3) is None
    message = compare_values(0.3333333333, 1 / 3, tolerance=1e-12)
    assert message is not None
    assert "tolerance 1e-12" in message


def test_compare_handles_infinities_and_nan() -> None:
    assert compare_values(-math.inf, -math.inf) is None
    assert compare_values(math.nan, math.nan) is None
    assert compare_values(math.inf, -math.inf) is not None


def test_compare_reports_paths() -> None:
    assert "result[1]" in (compare_values([1, 2, 3], [1, 9, 3]) or "")
    assert "result.a.b" in (compare_values({"a": {"b": 1}}, {"a": {"b": 2}}) or "")
    assert "missing b" in (compare_values({"a": 1}, {"a": 1, "b": 2}) or "")
    assert "unexpected c" in (compare_values({"a": 1, "c": 3}, {"a": 1}) or "")
    assert "expected 3 element(s), got 2" in (compare_values([1, 2], [1, 2, 3]) or "")


def test_compare_accepts_an_array_against_a_json_list() -> None:
    assert compare_values(array("f", [0.5, 0.25]), [0.5, 0.25]) is None


# --- the runner --------------------------------------------------------------


def test_runner_passes_a_good_case() -> None:
    result = run_vectors(
        FakePolicy,
        vectors_file(
            [
                {
                    "name": "smoke",
                    "params": {"capacity": 3},
                    "seed": 1,
                    "steps": [
                        {"call": "onAccess", "args": ["a", False]},
                        {"call": "onAccess", "args": ["b", False]},
                        {"call": "size", "expect": 2},
                        {"call": "evict", "expect": "a"},
                    ],
                }
            ]
        ),
    )
    assert result.failures == []
    assert result.cases_run == 1
    assert result.steps_run == 4
    assert result.assertions_run == 2


def test_runner_maps_camel_case_calls_to_snake_case_methods() -> None:
    # The vector says onAccess; the Python policy spells it on_access.
    result = run_vectors(
        FakePolicy,
        vectors_file(
            [{"name": "mapping", "steps": [{"call": "onAccess", "args": ["a", False]},
                                           {"call": "size", "expect": 1}]}]
        ),
    )
    assert result.failures == []


def test_runner_reports_a_mismatch_with_both_values() -> None:
    result = run_vectors(
        FakePolicy,
        vectors_file([{"name": "bad", "steps": [{"call": "size", "expect": 9}]}]),
    )
    assert len(result.failures) == 1
    assert "expected 9" in result.failures[0].message
    assert "got 0" in result.failures[0].message
    assert "[bad]" in result.report()


def test_runner_names_a_missing_method_and_lists_alternatives() -> None:
    result = run_vectors(
        FakePolicy,
        vectors_file([{"name": "typo", "steps": [{"call": "onAcess", "args": ["a"]}]}]),
    )
    assert len(result.failures) == 1
    message = result.failures[0].message
    assert 'no method "on_acess"' in message
    assert "on_access" in message


def test_runner_reports_a_method_that_raises() -> None:
    result = run_vectors(
        FakePolicy, vectors_file([{"name": "boom", "steps": [{"call": "explode"}]}])
    )
    assert "threw: boom" in result.failures[0].message


def test_runner_reports_a_constructor_that_raises() -> None:
    result = run_vectors(
        FakePolicy,
        vectors_file([{"name": "ctor", "params": {"nope": 1}, "steps": [{"call": "size"}]}]),
    )
    assert result.failures[0].step_index == -1
    assert "constructing the policy threw" in result.failures[0].message


def test_runner_treats_expect_null_as_an_assertion() -> None:
    passing = run_vectors(
        FakePolicy, vectors_file([{"name": "null", "steps": [{"call": "maybe", "expect": None}]}])
    )
    assert passing.failures == []
    assert passing.assertions_run == 1

    failing = run_vectors(
        FakePolicy, vectors_file([{"name": "null", "steps": [{"call": "size", "expect": None}]}])
    )
    assert len(failing.failures) == 1


def test_runner_passes_params_and_seeds_the_rng() -> None:
    result = run_vectors(
        FakePolicy,
        vectors_file(
            [
                {
                    "name": "params",
                    "params": {"capacity": 7},
                    "seed": 42,
                    "steps": [
                        {"call": "configuredCapacity", "expect": 7},
                        {"call": "draw", "expect": Rng(42).next_u32()},
                    ],
                }
            ]
        ),
    )
    assert result.failures == []


def test_runner_omits_rng_for_policies_that_do_not_want_one() -> None:
    # A deterministic policy should not have to declare a parameter it never
    # reads, so the runner inspects the signature rather than forcing it.
    result = run_vectors(
        NoRngPolicy,
        vectors_file(
            [{"name": "no rng", "params": {"capacity": 5},
              "steps": [{"call": "configuredCapacity", "expect": 5}]}]
        ),
    )
    assert result.failures == []


def test_load_policy_class_explains_an_ambiguous_module(tmp_path: Path) -> None:
    module = tmp_path / "policy.py"
    module.write_text("class One:\n    pass\n\n\nclass Two:\n    pass\n", encoding="utf-8")
    with pytest.raises(ImportError, match="exactly one public policy class"):
        load_policy_class(module)


def test_load_policy_class_finds_the_single_class(tmp_path: Path) -> None:
    module = tmp_path / "policy.py"
    module.write_text(
        "class Only:\n    def evict(self) -> int:\n        return 1\n", encoding="utf-8"
    )
    assert load_policy_class(module).__name__ == "Only"


def test_load_policy_class_honours_an_explicit_marker(tmp_path: Path) -> None:
    module = tmp_path / "policy.py"
    module.write_text(
        "class Helper:\n    pass\n\n\nclass Real:\n    pass\n\n\nPOLICY = Real\n",
        encoding="utf-8",
    )
    assert load_policy_class(module).__name__ == "Real"


# --- the registry sweep ------------------------------------------------------


def discover_python_policies() -> list[Path]:
    """Every policy directory declaring a ``python`` port."""
    policies_root = REPO_ROOT / "policies"
    if not policies_root.is_dir():
        return []

    found: list[Path] = []
    for meta_path in sorted(policies_root.glob("*/*/policy.json")):
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
        if "python" in meta.get("ports", []):
            found.append(meta_path.parent)
    return found


PYTHON_POLICIES = discover_python_policies()


def test_registry_is_readable() -> None:
    assert isinstance(PYTHON_POLICIES, list)


@pytest.mark.skipif(not PYTHON_POLICIES, reason="no Python policies in the catalog yet")
@pytest.mark.parametrize(
    "policy_dir", PYTHON_POLICIES, ids=[str(p.parent.name + "/" + p.name) for p in PYTHON_POLICIES]
)
def test_policy_passes_its_vectors(policy_dir: Path) -> None:
    """The registry's own file, loaded by path, satisfies its vectors."""
    vectors = json.loads((policy_dir / "vectors.json").read_text(encoding="utf-8"))
    result = run_vectors(load_policy_class(policy_dir / "policy.py"), vectors)
    assert not result.failures, result.report()
    assert result.assertions_run > 0


@pytest.mark.skipif(not PYTHON_POLICIES, reason="no Python policies in the catalog yet")
@pytest.mark.parametrize(
    "policy_dir", PYTHON_POLICIES, ids=[str(p.parent.name + "/" + p.name) for p in PYTHON_POLICIES]
)
def test_assembled_package_passes_the_same_vectors(policy_dir: Path) -> None:
    """The *shipped* class satisfies them too.

    The test above proves the file in ``policies/`` is correct; this one proves
    that what a user actually installs is the same thing. A stale assembly would
    pass the first and fail here, which is the whole reason both exist.
    """
    expected = load_policy_class(policy_dir / "policy.py")
    domain = policy_dir.parent.name.replace("-", "_")

    package = importlib.import_module(f"policybook.{domain}")
    shipped = getattr(package, expected.__name__, None)
    assert shipped is not None, (
        f"policybook.{domain} does not export {expected.__name__}. "
        "Run `pnpm tsx scripts/assemble-python.ts`."
    )

    vectors = json.loads((policy_dir / "vectors.json").read_text(encoding="utf-8"))
    result = run_vectors(shipped, vectors)
    assert not result.failures, result.report()
    assert result.assertions_run > 0

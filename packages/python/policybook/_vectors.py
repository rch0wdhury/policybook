"""The Python vector runner.

A policy's ``vectors.json`` is a list of method calls and expected results.
Every language has one generic runner that reflects on method names, so adding
an implementation means adding one file, not a new test. This is that runner
for Python.

The vector files are written in the reference (TypeScript) spelling, so method
names arrive as ``onAccess`` and are mapped to ``on_access`` here.

Run it directly against a policy directory::

    python -m policybook._vectors policies/cache/sieve
"""

from __future__ import annotations

import importlib.util
import inspect
import json
import math
import sys
from array import array
from pathlib import Path
from typing import Any

from policybook.rng import Rng

__all__ = [
    "VectorFailure",
    "VectorResult",
    "camel_to_snake",
    "compare_values",
    "load_policy_class",
    "run_vectors",
    "snake_case_keys",
]

DEFAULT_TOLERANCE = 1e-9
"""Absolute tolerance for float comparisons, unless a case overrides it."""


def camel_to_snake(name: str) -> str:
    """Map a reference method name to its Python spelling.

    ``onAccess`` becomes ``on_access``. Idiomatic naming per language is allowed
    for methods, and this is the mapping that permits it.
    """
    out: list[str] = []
    for index, character in enumerate(name):
        if character.isupper():
            if index > 0:
                out.append("_")
            out.append(character.lower())
        else:
            out.append(character)
    return "".join(out)


def snake_case_keys(value: object) -> object:
    """Map the keys of a structured argument to their Python spelling.

    Vectors are language-neutral and spell everything the reference way, so an
    argument like the retry domain's error object arrives as
    ``{"retryAfterMs": 250}``. Methods and parameter names are already mapped at
    this boundary; a dictionary passed *as* an argument is the same situation
    one level down, and translating it here is what lets a Python policy read
    ``error["retry_after_ms"]`` like the rest of the language.

    Applied recursively, so a nested structure maps throughout. Values are left
    alone — only keys are names.
    """
    if isinstance(value, dict):
        return {camel_to_snake(str(key)): snake_case_keys(item) for key, item in value.items()}
    if isinstance(value, list):
        return [snake_case_keys(item) for item in value]
    return value


def _is_sequence(value: object) -> bool:
    """True for things a JSON array should compare against."""
    return isinstance(value, (list, tuple, array))


def describe_value(value: object) -> str:
    """Render a value for an error message."""
    if isinstance(value, str):
        return json.dumps(value)
    if isinstance(value, array):
        return f"array({list(value)})"
    return repr(value)


def compare_values(
    actual: object,
    expected: object,
    tolerance: float = DEFAULT_TOLERANCE,
    path: str = "result",
) -> str | None:
    """Compare a return value against a vector's expectation.

    Returns ``None`` when they match, otherwise a message naming the path and
    both values.
    """
    # Booleans first: in Python ``True == 1``, which would let a bool satisfy a
    # numeric expectation (and vice versa) if numbers were checked first.
    if isinstance(expected, bool) or isinstance(actual, bool):
        if isinstance(expected, bool) and isinstance(actual, bool) and actual == expected:
            return None
        return f"{path}: expected {describe_value(expected)}, got {describe_value(actual)}"

    if isinstance(expected, (int, float)):
        if not isinstance(actual, (int, float)):
            return (
                f"{path}: expected the number {describe_value(expected)}, "
                f"got {describe_value(actual)}"
            )
        if actual == expected:
            return None
        if math.isnan(float(actual)) and math.isnan(float(expected)):
            return None
        difference = abs(float(actual) - float(expected))
        if math.isfinite(difference) and difference <= tolerance:
            return None
        suffix = ""
        if math.isfinite(difference):
            suffix = f" (off by {difference}, tolerance {tolerance})"
        return f"{path}: expected {describe_value(expected)}, got {describe_value(actual)}{suffix}"

    if _is_sequence(expected):
        expected_list = list(expected)  # type: ignore[call-overload]
        if not _is_sequence(actual):
            return (
                f"{path}: expected an array of {len(expected_list)}, "
                f"got {describe_value(actual)}"
            )
        actual_list = list(actual)  # type: ignore[call-overload]
        if len(actual_list) != len(expected_list):
            return (
                f"{path}: expected {len(expected_list)} element(s), got {len(actual_list)} "
                f"— expected {describe_value(expected)}, got {describe_value(actual)}"
            )
        # strict=True is safe: the lengths were just checked to be equal.
        for index, (left, right) in enumerate(zip(actual_list, expected_list, strict=True)):
            message = compare_values(left, right, tolerance, f"{path}[{index}]")
            if message is not None:
                return message
        return None

    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            return f"{path}: expected an object, got {describe_value(actual)}"
        expected_keys = sorted(expected)
        actual_keys = sorted(actual)
        missing = [key for key in expected_keys if key not in actual_keys]
        extra = [key for key in actual_keys if key not in expected_keys]
        if missing or extra:
            parts = []
            if missing:
                parts.append(f"missing {', '.join(missing)}")
            if extra:
                parts.append(f"unexpected {', '.join(extra)}")
            return f"{path}: object keys differ ({'; '.join(parts)})"
        for key in expected_keys:
            message = compare_values(actual[key], expected[key], tolerance, f"{path}.{key}")
            if message is not None:
                return message
        return None

    if actual == expected:
        return None
    return f"{path}: expected {describe_value(expected)}, got {describe_value(actual)}"


class VectorFailure:
    """One thing that went wrong, located precisely enough to fix."""

    __slots__ = ("call", "case_name", "message", "step_index")

    def __init__(self, case_name: str, step_index: int, call: str, message: str) -> None:
        self.case_name = case_name
        self.step_index = step_index
        self.call = call
        self.message = message

    def __str__(self) -> str:
        where = "construction" if self.step_index == -1 else f"step {self.step_index}"
        return f"  [{self.case_name}] {where}: {self.message}"


class VectorResult:
    """What a run produced: counts for reporting, failures for asserting."""

    __slots__ = ("assertions_run", "cases_run", "failures", "policy", "steps_run")

    def __init__(self, policy: str) -> None:
        self.policy = policy
        self.cases_run = 0
        self.steps_run = 0
        self.assertions_run = 0
        self.failures: list[VectorFailure] = []

    def report(self) -> str:
        """Format the failures as a block of text."""
        if not self.failures:
            return ""
        header = (
            f"{self.policy}: {len(self.failures)} of {self.assertions_run} assertion(s) failed"
        )
        return "\n".join([header, *(str(failure) for failure in self.failures)])


def _method_names(policy: object) -> list[str]:
    """Callable attributes on the policy, for a helpful error message."""
    return sorted(
        name
        for name in dir(policy)
        if not name.startswith("_") and callable(getattr(policy, name, None))
    )


def _construct(policy_cls: type, params: dict[str, Any], rng: Rng) -> object:
    """Build the policy, passing ``rng`` only if its constructor wants one.

    Deterministic policies should not have to declare a parameter they never
    read, so this inspects the signature rather than forcing every policy to
    accept one.

    Parameter names are mapped from the reference spelling the same way method
    names are: a vector saying ``windowFraction`` reaches a Python policy as
    ``window_fraction``. Without this, every multi-word parameter would have to
    be spelled in camelCase throughout the Python package.
    """
    named = {camel_to_snake(name): value for name, value in params.items()}

    # Signature of the class, not of __init__: it already excludes ``self``, and
    # asking a class object for its __init__ is unsound for subclasses.
    signature = inspect.signature(policy_cls)
    accepts_rng = "rng" in signature.parameters or any(
        parameter.kind is inspect.Parameter.VAR_KEYWORD
        for parameter in signature.parameters.values()
    )
    if accepts_rng:
        return policy_cls(rng=rng, **named)
    return policy_cls(**named)


def run_vectors(policy_cls: type, vectors: dict[str, Any]) -> VectorResult:
    """Run every case in ``vectors`` against ``policy_cls``.

    Never raises for a failed expectation: failures are collected so one run
    reports everything that is wrong.
    """
    result = VectorResult(str(vectors.get("policy", "<unknown>")))

    for case in vectors.get("cases", []):
        result.cases_run += 1
        name = str(case.get("name", "<unnamed>"))
        tolerance = float(case.get("tolerance", DEFAULT_TOLERANCE))
        params = dict(case.get("params", {}))
        rng = Rng(int(case.get("seed", 0)))

        try:
            policy = _construct(policy_cls, params, rng)
        except Exception as error:
            result.failures.append(
                VectorFailure(name, -1, "constructor", f"constructing the policy threw: {error}")
            )
            continue

        for index, step in enumerate(case.get("steps", [])):
            call = str(step["call"])
            attribute = camel_to_snake(call)
            method = getattr(policy, attribute, None)
            if method is None:
                # Tolerate a port that kept the reference spelling.
                method = getattr(policy, call, None)
            if not callable(method):
                result.failures.append(
                    VectorFailure(
                        name,
                        index,
                        call,
                        f'the policy has no method "{attribute}". Implement it, or fix the '
                        f"vector. Available: {', '.join(_method_names(policy)) or 'none'}",
                    )
                )
                break

            args = [snake_case_keys(arg) for arg in step.get("args", [])]
            try:
                actual = method(*args)
            except Exception as error:
                rendered = ", ".join(describe_value(arg) for arg in args)
                result.failures.append(
                    VectorFailure(name, index, call, f"{attribute}({rendered}) threw: {error}")
                )
                break
            result.steps_run += 1

            # ``expect: null`` is a real assertion, so test for the key.
            if "expect" not in step:
                continue
            result.assertions_run += 1

            message = compare_values(actual, step["expect"], tolerance)
            if message is not None:
                rendered = ", ".join(describe_value(arg) for arg in args)
                result.failures.append(
                    VectorFailure(name, index, call, f"{attribute}({rendered}) — {message}")
                )

    return result


def load_policy_class(module_path: Path) -> type:
    """Import a ``policy.py`` by path and return the policy class it defines.

    The class is found by convention: a module-level ``POLICY`` name if present,
    otherwise the single public class the module itself defines.
    """
    spec = importlib.util.spec_from_file_location(f"_pb_{module_path.parent.name}", module_path)
    if spec is None or spec.loader is None:
        msg = f"cannot import {module_path}"
        raise ImportError(msg)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    explicit = getattr(module, "POLICY", None)
    if isinstance(explicit, type):
        return explicit

    defined = [
        value
        for name, value in vars(module).items()
        if not name.startswith("_")
        and isinstance(value, type)
        and value.__module__ == module.__name__
    ]
    if len(defined) == 1:
        return defined[0]

    found = ", ".join(cls.__name__ for cls in defined) or "none"
    msg = (
        f"{module_path} must define exactly one public policy class, or set POLICY "
        f"to the right one. Found: {found}"
    )
    raise ImportError(msg)


def main(argv: list[str] | None = None) -> int:
    """Run one policy directory's vectors. Returns a process exit code."""
    args = sys.argv[1:] if argv is None else argv
    if len(args) != 1:
        print("usage: python -m policybook._vectors <policy directory>", file=sys.stderr)
        return 2

    directory = Path(args[0]).resolve()
    vectors_path = directory / "vectors.json"
    policy_path = directory / "policy.py"

    for required in (vectors_path, policy_path):
        if not required.exists():
            print(f"{required} does not exist", file=sys.stderr)
            return 2

    vectors = json.loads(vectors_path.read_text(encoding="utf-8"))
    result = run_vectors(load_policy_class(policy_path), vectors)

    if result.failures:
        print(result.report(), file=sys.stderr)
        return 1

    print(f"{result.policy}: {result.assertions_run} assertion(s) passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

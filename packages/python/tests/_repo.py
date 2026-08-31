"""Locating the repository root from inside the test suite."""

from __future__ import annotations

from pathlib import Path


def find_repo_root(start: Path) -> Path:
    """Walk up from ``start`` looking for ``pnpm-workspace.yaml``.

    The same rule the TypeScript side uses, so both languages agree on where the
    registry lives.
    """
    current = start.resolve()
    for candidate in [current, *current.parents]:
        if (candidate / "pnpm-workspace.yaml").exists():
            return candidate
    msg = (
        f"could not find the repository root above {start} "
        "(looked for pnpm-workspace.yaml in every parent directory)"
    )
    raise RuntimeError(msg)

"""Shared fixtures: locating the repository from inside the installed package."""

from __future__ import annotations

from pathlib import Path

import pytest
from _repo import find_repo_root


@pytest.fixture(scope="session")
def repo_root() -> Path:
    """Absolute path to the repository root."""
    return find_repo_root(Path(__file__).parent)

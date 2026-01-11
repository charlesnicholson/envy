"""Shared configuration for functional tests."""

import os
import sys
from pathlib import Path

# Required manifest header for all manifests (bin is mandatory)
MANIFEST_HEADER = '-- @envy bin "envy-bin"\n'

# Manifest header with deployment enabled (for sync tests)
MANIFEST_HEADER_DEPLOY = '-- @envy bin "envy-bin"\n-- @envy deploy "true"\n'


def make_manifest(packages_content: str, deploy: bool = False) -> str:
    """Create a manifest string with required headers.

    Args:
        packages_content: The PACKAGES table content (should start with 'PACKAGES = {')
        deploy: If True, include deploy directive for product script creation

    Returns:
        Complete manifest string with required bin directive.
    """
    header = MANIFEST_HEADER_DEPLOY if deploy else MANIFEST_HEADER
    return header + packages_content


_cached_executable: Path | None = None


def get_envy_executable() -> Path:
    """Get the path to the envy functional test executable."""
    global _cached_executable
    if _cached_executable is not None:
        return _cached_executable

    root = Path(__file__).parent.parent / "out" / "build"

    if sys.platform == "win32":
        exe = root / "envy_functional_tester.exe"
    else:
        exe = root / "envy_functional_tester"

    if not exe.exists():
        raise RuntimeError(
            f"Functional tester not found at {exe}. "
            "Build with: ./build.sh (or ./build.bat on Windows)"
        )

    if sys.platform != "win32" and not os.access(exe, os.X_OK):
        raise RuntimeError(f"Functional tester at {exe} is not executable")

    _cached_executable = exe
    return exe


def get_test_env() -> dict[str, str]:
    """Get environment variables for running tests."""
    env = os.environ.copy()
    root = Path(__file__).parent.parent

    # Point sanitizers to suppression files
    tsan_supp = root / "tsan.supp"
    if tsan_supp.exists():
        env.setdefault("TSAN_OPTIONS", f"suppressions={tsan_supp}")

    asan_supp = root / "asan.supp"
    if asan_supp.exists():
        env.setdefault("ASAN_OPTIONS", f"suppressions={asan_supp}")

    return env

"""Shared configuration for functional tests."""

import os
import sys
import threading
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


# Thread-local state for which sanitizer variant to use
_thread_local = threading.local()

# Cache of discovered functional testers
_discovered_testers: list[tuple[str, Path]] | None = None


def discover_functional_testers() -> list[tuple[str, Path]]:
    """Discover available functional tester executables.

    Returns:
        List of (variant_name, executable_path) tuples.
        variant_name is empty string for non-suffixed executables,
        otherwise it's the suffix after "envy_functional_tester_".
    """
    global _discovered_testers
    if _discovered_testers is not None:
        return _discovered_testers

    root = Path(__file__).parent.parent / "out" / "build"
    pattern = (
        "envy_functional_tester*.exe"
        if sys.platform == "win32"
        else "envy_functional_tester*"
    )

    testers = []
    for exe in sorted(root.glob(pattern)):
        # Skip if it's a directory or doesn't have execute permissions (Unix)
        if exe.is_dir():
            continue
        if sys.platform != "win32" and not os.access(exe, os.X_OK):
            continue

        # Extract variant name from filename
        stem = exe.stem  # Remove .exe if present
        if stem == "envy_functional_tester":
            # No suffix - single variant (e.g., Windows MSVC with just ASan)
            variant = ""
        elif stem.startswith("envy_functional_tester_"):
            # Extract suffix as variant name
            variant = stem[len("envy_functional_tester_") :]
        else:
            # Skip unrecognized patterns
            continue

        testers.append((variant, exe))

    if not testers:
        raise RuntimeError(
            f"No functional tester executables found in {root}. "
            "Build the project first with build.sh or build.bat"
        )

    _discovered_testers = testers
    return testers


def get_sanitizer_variants() -> list[str]:
    """Get list of available sanitizer variants.

    Returns:
        List of variant names (e.g., ["asan_ubsan", "tsan_ubsan"] or [""])
    """
    return [variant for variant, _ in discover_functional_testers()]


def set_sanitizer_variant(variant: str) -> None:
    """Set which sanitizer variant to use for tests.

    Args:
        variant: Variant name from get_sanitizer_variants()
    """
    _thread_local.sanitizer_variant = variant


def get_envy_executable() -> Path:
    """Get the path to the envy functional test executable.

    Returns the executable for the currently active sanitizer variant.
    """
    testers = discover_functional_testers()

    # Get current variant or use first available as default
    current_variant = getattr(_thread_local, "sanitizer_variant", testers[0][0])

    # Find matching tester
    for variant, exe_path in testers:
        if variant == current_variant:
            return exe_path

    # Fallback to first tester if variant not found
    return testers[0][1]


def get_test_env() -> dict[str, str]:
    """Get environment variables for running tests with the current sanitizer variant.

    Returns a copy of os.environ with sanitizer-specific options set.
    """
    env = os.environ.copy()
    variant = getattr(_thread_local, "sanitizer_variant", "")
    root = Path(__file__).parent.parent

    if "tsan" in variant:
        # Set TSAN_OPTIONS with suppression file
        tsan_supp = root / "tsan.supp"
        env["TSAN_OPTIONS"] = f"suppressions={tsan_supp}"

    return env

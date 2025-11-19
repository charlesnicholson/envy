"""Shared configuration for functional tests."""

import os
import sys
import threading
from pathlib import Path

# Thread-local state for which sanitizer variant to use
_thread_local = threading.local()


def set_sanitizer_variant(variant: str) -> None:
    """Set which sanitizer variant to use for tests.

    Args:
        variant: Either "asan_ubsan" or "tsan_ubsan"
    """
    _thread_local.sanitizer_variant = variant


def get_envy_executable() -> Path:
    """Get the path to the envy functional test executable.

    Returns the executable for the currently active sanitizer variant.
    """
    # Default to asan_ubsan if not set (for sequential/single test runs)
    variant = getattr(_thread_local, 'sanitizer_variant', 'asan_ubsan')

    root = Path(__file__).parent.parent / "out" / "build"
    executable_name = f"envy_functional_tester_{variant}"

    if sys.platform == "win32":
        executable_name += ".exe"

    return root / executable_name


def get_test_env() -> dict[str, str]:
    """Get environment variables for running tests with the current sanitizer variant.

    Returns a copy of os.environ with sanitizer-specific options set.
    """
    env = os.environ.copy()
    variant = getattr(_thread_local, 'sanitizer_variant', 'asan_ubsan')
    root = Path(__file__).parent.parent

    if variant == 'tsan_ubsan':
        # Set TSAN_OPTIONS with suppression file
        tsan_supp = root / "tsan.supp"
        env['TSAN_OPTIONS'] = f'suppressions={tsan_supp}'

    return env

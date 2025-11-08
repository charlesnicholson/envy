from __future__ import annotations

import pathlib
import unittest


def load_tests(
    loader: unittest.TestLoader, tests: unittest.TestSuite, pattern: str | None
) -> unittest.TestSuite:
    start_dir = pathlib.Path(__file__).resolve().parent
    discovered = loader.discover(
        start_dir=str(start_dir),
        pattern=pattern or "test_*.py",
        top_level_dir=str(start_dir.parent),
    )
    tests.addTests(discovered)
    return tests

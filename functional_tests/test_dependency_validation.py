#!/usr/bin/env python3
"""Functional tests for ctx.asset() dependency validation.

Tests that recipes must explicitly declare dependencies (direct or transitive)
before calling ctx.asset() to access them. This ensures build graph integrity
and enables better dependency analysis.
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest


class TestDependencyValidation(unittest.TestCase):
    """Tests for ctx.asset() dependency validation."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-depval-test-"))
        self.envy_test = (
            Path(__file__).parent.parent / "out" / "build" / "envy_functional_tester"
        )
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)

    def test_direct_dependency_declared(self):
        """Recipe calls ctx.asset() on declared direct dependency - should succeed."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.dep_val_direct@v1",
                "test_data/recipes/dep_val_direct.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        # Success means validation passed - the build completed without error
        self.assertIn("local.dep_val_direct@v1", result.stdout)

    def test_missing_dependency_declaration(self):
        """Recipe calls ctx.asset() without declaring dependency - should fail."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.dep_val_missing@v1",
                "test_data/recipes/dep_val_missing.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(result.returncode, 0, "Expected failure for missing dependency")
        self.assertIn("does not declare dependency", result.stderr)
        self.assertIn("local.dep_val_missing@v1", result.stderr)
        self.assertIn("local.dep_val_lib@v1", result.stderr)

    def test_transitive_dependency(self):
        """Recipe calls ctx.asset() on transitive dependency - should succeed."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.dep_val_transitive@v1",
                "test_data/recipes/dep_val_transitive.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        # Success means validation passed - the build completed without error
        self.assertIn("local.dep_val_transitive@v1", result.stdout)


if __name__ == "__main__":
    unittest.main()

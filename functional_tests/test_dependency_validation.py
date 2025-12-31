#!/usr/bin/env python3
"""Functional tests for envy.asset() dependency validation.

Tests that specs must explicitly declare dependencies (direct or transitive)
before calling envy.asset() to access them. This ensures build graph integrity
and enables better dependency analysis.
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest

from . import test_config


class TestDependencyValidation(unittest.TestCase):
    """Tests for envy.asset() dependency validation."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-depval-test-"))
        self.envy_test = test_config.get_envy_executable()
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)

    def test_direct_dependency_declared(self):
        """Spec calls envy.asset() on declared direct dependency - should succeed."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.dep_val_direct@v1",
                "test_data/specs/dep_val_direct.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
        # Success means validation passed - the build completed without error
        self.assertIn("local.dep_val_direct@v1", result.stdout)

    def test_missing_dependency_declaration(self):
        """Spec calls envy.asset() without declaring dependency - should fail."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.dep_val_missing@v1",
                "test_data/specs/dep_val_missing.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(
            result.returncode, 0, "Expected failure for missing dependency"
        )
        self.assertIn("has no strong dependency on 'local.dep_val_lib@v1'", result.stderr)
        self.assertIn("local.dep_val_missing@v1", result.stderr)

    def test_transitive_dependency(self):
        """Spec calls envy.asset() on transitive dependency - should succeed."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.dep_val_transitive@v1",
                "test_data/specs/dep_val_transitive.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
        # Success means validation passed - the build completed without error
        self.assertIn("local.dep_val_transitive@v1", result.stdout)

    def test_transitive_3_levels(self):
        """Spec calls envy.asset() on transitive dependency 3 levels deep - should succeed."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.dep_val_level3_top@v1",
                "test_data/specs/dep_val_level3_top.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
        self.assertIn("local.dep_val_level3_top@v1", result.stdout)

    def test_diamond_dependency(self):
        """Spec accesses dependency via two different paths (diamond) - should succeed."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.dep_val_diamond_top@v1",
                "test_data/specs/dep_val_diamond_top.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
        self.assertIn("local.dep_val_diamond_top@v1", result.stdout)

    def test_deep_chain_5_levels(self):
        """Spec calls envy.asset() on dependency 5 levels deep - should succeed."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.dep_val_chain5_e@v1",
                "test_data/specs/dep_val_chain5_e.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
        self.assertIn("local.dep_val_chain5_e@v1", result.stdout)

    def test_unrelated_recipe_error(self):
        """Spec calls envy.asset() on unrelated spec - should fail."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.dep_val_unrelated@v1",
                "test_data/specs/dep_val_unrelated.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(
            result.returncode, 0, "Expected failure for unrelated spec"
        )
        self.assertIn("has no strong dependency on 'local.dep_val_lib@v1'", result.stderr)
        self.assertIn("local.dep_val_unrelated@v1", result.stderr)
        self.assertIn("local.dep_val_lib@v1", result.stderr)

    def test_needed_by_direct(self):
        """Spec with needed_by="recipe_fetch" calls envy.asset() on direct dep in fetch phase - should succeed."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.dep_val_needed_by_direct@v1",
                "test_data/specs/dep_val_needed_by_direct.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
        self.assertIn("local.dep_val_needed_by_direct@v1", result.stdout)

    def test_needed_by_transitive(self):
        """Spec with needed_by="recipe_fetch" calls envy.asset() on transitive dep in fetch phase - should succeed."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.dep_val_needed_by_transitive@v1",
                "test_data/specs/dep_val_needed_by_transitive.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
        self.assertIn("local.dep_val_needed_by_transitive@v1", result.stdout)

    def test_needed_by_undeclared(self):
        """Spec with needed_by="recipe_fetch" calls envy.asset() on undeclared dep - should fail."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.dep_val_needed_by_undeclared@v1",
                "test_data/specs/dep_val_needed_by_undeclared.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(
            result.returncode, 0, "Expected failure for undeclared dependency"
        )
        self.assertIn("has no strong dependency on 'local.dep_val_lib@v1'", result.stderr)
        self.assertIn("local.dep_val_needed_by_undeclared@v1", result.stderr)
        self.assertIn("local.dep_val_lib@v1", result.stderr)

    def test_parallel_validation(self):
        """Multiple specs sharing same base library, all validated in parallel - should all succeed."""
        # Set ENVY_TEST_JOBS to enable parallel execution
        env = os.environ.copy()
        env["ENVY_TEST_JOBS"] = "8"

        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.dep_val_parallel_manifest@v1",
                "test_data/specs/dep_val_parallel_manifest.lua",
            ],
            capture_output=True,
            text=True,
            env=env,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
        self.assertIn("local.dep_val_parallel_manifest@v1", result.stdout)

    def test_default_shell_with_dependency(self):
        """default_shell function calls envy.asset(), spec declares dependency - should succeed."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.dep_val_shell_with_dep@v1",
                "test_data/specs/dep_val_shell_with_dep.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
        self.assertIn("local.dep_val_shell_with_dep@v1", result.stdout)

    def test_deep_chain_parallel(self):
        """Deep transitive chain under parallel execution - validation doesn't race."""
        # Run the 5-level chain test with parallel jobs
        env = os.environ.copy()
        env["ENVY_TEST_JOBS"] = "8"

        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.dep_val_chain5_e@v1",
                "test_data/specs/dep_val_chain5_e.lua",
            ],
            capture_output=True,
            text=True,
            env=env,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
        self.assertIn("local.dep_val_chain5_e@v1", result.stdout)


if __name__ == "__main__":
    unittest.main()

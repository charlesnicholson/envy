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

    def test_transitive_3_levels(self):
        """Recipe calls ctx.asset() on transitive dependency 3 levels deep - should succeed."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.dep_val_level3_top@v1",
                "test_data/recipes/dep_val_level3_top.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("local.dep_val_level3_top@v1", result.stdout)

    def test_diamond_dependency(self):
        """Recipe accesses dependency via two different paths (diamond) - should succeed."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.dep_val_diamond_top@v1",
                "test_data/recipes/dep_val_diamond_top.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("local.dep_val_diamond_top@v1", result.stdout)

    def test_deep_chain_5_levels(self):
        """Recipe calls ctx.asset() on dependency 5 levels deep - should succeed."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.dep_val_chain5_e@v1",
                "test_data/recipes/dep_val_chain5_e.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("local.dep_val_chain5_e@v1", result.stdout)

    def test_unrelated_recipe_error(self):
        """Recipe calls ctx.asset() on unrelated recipe - should fail."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.dep_val_unrelated@v1",
                "test_data/recipes/dep_val_unrelated.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(result.returncode, 0, "Expected failure for unrelated recipe")
        self.assertIn("does not declare dependency", result.stderr)
        self.assertIn("local.dep_val_unrelated@v1", result.stderr)
        self.assertIn("local.dep_val_lib@v1", result.stderr)

    def test_needed_by_direct(self):
        """Recipe with needed_by="recipe_fetch" calls ctx.asset() on direct dep in fetch phase - should succeed."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.dep_val_needed_by_direct@v1",
                "test_data/recipes/dep_val_needed_by_direct.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("local.dep_val_needed_by_direct@v1", result.stdout)

    def test_needed_by_transitive(self):
        """Recipe with needed_by="recipe_fetch" calls ctx.asset() on transitive dep in fetch phase - should succeed."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.dep_val_needed_by_transitive@v1",
                "test_data/recipes/dep_val_needed_by_transitive.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("local.dep_val_needed_by_transitive@v1", result.stdout)

    def test_needed_by_undeclared(self):
        """Recipe with needed_by="recipe_fetch" calls ctx.asset() on undeclared dep - should fail."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.dep_val_needed_by_undeclared@v1",
                "test_data/recipes/dep_val_needed_by_undeclared.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(result.returncode, 0, "Expected failure for undeclared dependency")
        self.assertIn("does not declare dependency", result.stderr)
        self.assertIn("local.dep_val_needed_by_undeclared@v1", result.stderr)
        self.assertIn("local.dep_val_lib@v1", result.stderr)

    def test_parallel_validation(self):
        """Multiple recipes sharing same base library, all validated in parallel - should all succeed."""
        # Set ENVY_TEST_JOBS to enable parallel execution
        env = os.environ.copy()
        env["ENVY_TEST_JOBS"] = "8"

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.dep_val_parallel_manifest@v1",
                "test_data/recipes/dep_val_parallel_manifest.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
            env=env,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("local.dep_val_parallel_manifest@v1", result.stdout)

    def test_default_shell_with_dependency(self):
        """default_shell function calls ctx.asset(), recipe declares dependency - should succeed."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.dep_val_shell_with_dep@v1",
                "test_data/recipes/dep_val_shell_with_dep.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("local.dep_val_shell_with_dep@v1", result.stdout)

    def test_deep_chain_parallel(self):
        """Deep transitive chain under parallel execution - validation doesn't race."""
        # Run the 5-level chain test with parallel jobs
        env = os.environ.copy()
        env["ENVY_TEST_JOBS"] = "8"

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.dep_val_chain5_e@v1",
                "test_data/recipes/dep_val_chain5_e.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
            env=env,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("local.dep_val_chain5_e@v1", result.stdout)


if __name__ == "__main__":
    unittest.main()

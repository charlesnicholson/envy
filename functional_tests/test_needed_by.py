#!/usr/bin/env python3
"""Functional tests for needed_by phase dependencies.

Tests that needed_by annotation enables fine-grained parallelism by allowing
recipes to specify which phase they actually need a dependency for. For example,
A depends on B with needed_by="build" means A's fetch/stage can run in parallel
with B's pipeline, blocking only when A needs to build.
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest


class TestNeededBy(unittest.TestCase):
    """Tests for needed_by phase coupling."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-needed-by-test-"))
        self.envy_test = (
            Path(__file__).parent.parent / "out" / "build" / "envy_functional_tester"
        )
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)

    def test_needed_by_fetch_allows_parallelism(self):
        """Recipe A depends on B with needed_by='fetch' - A's early phases run in parallel."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.needed_by_fetch_parent@v1",
                "test_data/recipes/needed_by_fetch_parent.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        # Verify both recipes processed
        self.assertIn("local.needed_by_fetch_parent@v1", result.stdout)
        self.assertIn("local.needed_by_fetch_dep@v1", result.stdout)

    def test_needed_by_build(self):
        """Recipe A depends on B with needed_by='build' - fetch/stage parallel, build waits."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.needed_by_build_parent@v1",
                "test_data/recipes/needed_by_build_parent.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("local.needed_by_build_parent@v1", result.stdout)
        self.assertIn("local.needed_by_build_dep@v1", result.stdout)

    def test_needed_by_stage(self):
        """Recipe A depends on B with needed_by='stage' - fetch parallel, stage waits."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.needed_by_stage_parent@v1",
                "test_data/recipes/needed_by_stage_parent.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("local.needed_by_stage_parent@v1", result.stdout)
        self.assertIn("local.needed_by_stage_dep@v1", result.stdout)

    def test_needed_by_install(self):
        """Recipe A depends on B with needed_by='install' - fetch/stage/build parallel, install waits."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.needed_by_install_parent@v1",
                "test_data/recipes/needed_by_install_parent.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("local.needed_by_install_parent@v1", result.stdout)
        self.assertIn("local.needed_by_install_dep@v1", result.stdout)

    def test_needed_by_deploy(self):
        """Recipe A depends on B with needed_by='deploy' - all phases parallel except deploy."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.needed_by_deploy_parent@v1",
                "test_data/recipes/needed_by_deploy_parent.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("local.needed_by_deploy_parent@v1", result.stdout)
        self.assertIn("local.needed_by_deploy_dep@v1", result.stdout)

    def test_needed_by_check(self):
        """Recipe A depends on B with needed_by='check' - check waits, rest runs parallel."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.needed_by_check_parent@v1",
                "test_data/recipes/needed_by_check_parent.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("local.needed_by_check_parent@v1", result.stdout)
        self.assertIn("local.needed_by_check_dep@v1", result.stdout)

    def test_needed_by_default_to_check(self):
        """Recipe A depends on B without needed_by - defaults to conservative check phase."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.needed_by_default_parent@v1",
                "test_data/recipes/needed_by_default_parent.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("local.needed_by_default_parent@v1", result.stdout)
        self.assertIn("local.simple@v1", result.stdout)

    def test_needed_by_invalid_phase_name(self):
        """Recipe with needed_by='nonexistent' fails during parsing."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.needed_by_invalid@v1",
                "test_data/recipes/needed_by_invalid.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(result.returncode, 0, "Expected invalid phase name to fail")
        self.assertIn("needed_by", result.stderr.lower())

    def test_needed_by_multi_level_chain(self):
        """Multi-level chain: A→B→C with different needed_by phases."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.needed_by_chain_a@v1",
                "test_data/recipes/needed_by_chain_a.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("local.needed_by_chain_a@v1", result.stdout)
        self.assertIn("local.needed_by_chain_b@v1", result.stdout)
        self.assertIn("local.needed_by_chain_c@v1", result.stdout)

    def test_needed_by_diamond(self):
        """Diamond: A depends on B+C with different needed_by phases."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.needed_by_diamond_a@v1",
                "test_data/recipes/needed_by_diamond_a.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("local.needed_by_diamond_a@v1", result.stdout)
        self.assertIn("local.needed_by_diamond_b@v1", result.stdout)
        self.assertIn("local.needed_by_diamond_c@v1", result.stdout)

    def test_needed_by_race_condition(self):
        """Dependency completes before parent discovers it - late edge addition handled."""
        # This tests the if (dep_acc->second.completed) { try_put() } logic
        # Run with parallel jobs to increase chance of race
        env = os.environ.copy()
        env["ENVY_TEST_JOBS"] = "8"

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.needed_by_race_parent@v1",
                "test_data/recipes/needed_by_race_parent.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
            env=env,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("local.needed_by_race_parent@v1", result.stdout)

    def test_needed_by_with_cache_hit(self):
        """Recipe with needed_by where dependency is already cached."""
        # First run: cache miss
        result1 = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.needed_by_cached_parent@v1",
                "test_data/recipes/needed_by_cached_parent.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result1.returncode, 0, f"stdout: {result1.stdout}\nstderr: {result1.stderr}")

        # Second run: cache hit (should still respect needed_by)
        result2 = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.needed_by_cached_parent@v1",
                "test_data/recipes/needed_by_cached_parent.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result2.returncode, 0, f"stdout: {result2.stdout}\nstderr: {result2.stderr}")
        self.assertIn("local.needed_by_cached_parent@v1", result2.stdout)

    def test_needed_by_all_phases(self):
        """Recipe with multiple dependencies using different needed_by phases."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.needed_by_all_phases@v1",
                "test_data/recipes/needed_by_all_phases.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        # Verify all recipes processed
        self.assertIn("local.needed_by_all_phases@v1", result.stdout)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Functional tests for engine dependency resolution.

Tests the dependency graph construction phase: building dependency graphs,
cycle detection, memoization, and local/remote security constraints.
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest


class TestEngineDependencyResolution(unittest.TestCase):
    """Tests for dependency graph construction and validation."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-engine-test-"))
        self.envy_test = (
            Path(__file__).parent.parent / "out" / "build" / "envy_functional_tester"
        )
        self.envy = Path(__file__).parent.parent / "out" / "build" / "envy"
        # Enable trace for all tests if ENVY_TEST_TRACE is set
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)

    def get_file_hash(self, filepath):
        """Get SHA256 hash of file using envy hash command."""
        result = subprocess.run(
            [str(self.envy), "hash", str(filepath)],
            capture_output=True,
            text=True,
            check=True,
        )
        return result.stdout.strip()

    def test_recipe_with_one_dependency(self):
        """Engine loads recipe and its dependency."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.withdep@v1",
                "test_data/recipes/with_dep.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 2, f"Expected 2 recipes, got: {result.stdout}")

        # Check both identities present
        output = dict(line.split(" -> ", 1) for line in lines)
        self.assertIn("local.withdep@v1", output)
        self.assertIn("local.simple@v1", output)

    def test_cycle_detection(self):
        """Engine detects and rejects dependency cycles."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.cycle_a@v1",
                "test_data/recipes/cycle_a.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(result.returncode, 0, "Expected cycle to cause failure")
        self.assertIn(
            "cycle",
            result.stderr.lower(),
            f"Expected cycle error, got: {result.stderr}",
        )

    def test_diamond_dependency_memoization(self):
        """Engine memoizes shared dependencies (diamond: A->B,C; B,C->D)."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.diamond_a@v1",
                "test_data/recipes/diamond_a.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stderr: {result.stderr}\n\nstdout: {result.stdout}"
        )

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(
            len(lines), 4, f"Expected 4 recipes (A,B,C,D once), got: {result.stdout}"
        )

        # Verify all present - parse with better error reporting
        output = {}
        for i, line in enumerate(lines):
            parts = line.split(" -> ", 1)
            self.assertEqual(
                len(parts),
                2,
                f"Line {i} malformed (expected 'key -> value'): {repr(line)}\nAll lines: {lines}",
            )
            output[parts[0]] = parts[1]
        self.assertIn("local.diamond_a@v1", output)
        self.assertIn("local.diamond_b@v1", output)
        self.assertIn("local.diamond_c@v1", output)
        self.assertIn("local.diamond_d@v1", output)

    def test_multiple_independent_roots(self):
        """Engine resolves multiple independent dependency trees."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.multiple_roots@v1",
                "test_data/recipes/multiple_roots.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 3, f"Expected 3 recipes, got: {result.stdout}")

        # Verify all present
        output = dict(line.split(" -> ", 1) for line in lines)
        self.assertIn("local.multiple_roots@v1", output)
        self.assertIn("local.independent_left@v1", output)
        self.assertIn("local.independent_right@v1", output)

    def test_options_differentiate_recipes(self):
        """Same recipe identity with different options creates separate entries."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.options_parent@v1",
                "test_data/recipes/options_parent.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(
            len(lines),
            3,
            f"Expected 3 recipes (parent + 2 variants), got: {result.stdout}",
        )

        # Verify all present with options in keys
        output = dict(line.split(" -> ", 1) for line in lines)
        self.assertIn("local.options_parent@v1", output)
        self.assertIn("local.with_options@v1{variant=bar}", output)
        self.assertIn("local.with_options@v1{variant=foo}", output)

    def test_deep_chain_dependency(self):
        """Engine resolves deep dependency chain (A->B->C->D->E)."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.chain_a@v1",
                "test_data/recipes/chain_a.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(
            len(lines), 5, f"Expected 5 recipes (A,B,C,D,E), got: {result.stdout}"
        )

        # Verify all present
        output = dict(line.split(" -> ", 1) for line in lines)
        self.assertIn("local.chain_a@v1", output)
        self.assertIn("local.chain_b@v1", output)
        self.assertIn("local.chain_c@v1", output)
        self.assertIn("local.chain_d@v1", output)
        self.assertIn("local.chain_e@v1", output)

    def test_wide_fanout_dependency(self):
        """Engine resolves wide fan-out (root->child1,2,3,4)."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.fanout_root@v1",
                "test_data/recipes/fanout_root.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 5, f"Expected 5 recipes, got: {result.stdout}")

        # Verify all present
        output = dict(line.split(" -> ", 1) for line in lines)
        self.assertIn("local.fanout_root@v1", output)
        self.assertIn("local.fanout_child1@v1", output)
        self.assertIn("local.fanout_child2@v1", output)
        self.assertIn("local.fanout_child3@v1", output)
        self.assertIn("local.fanout_child4@v1", output)

    def test_nonlocal_cannot_depend_on_local(self):
        """Engine rejects non-local recipe depending on local.*."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "remote.badrecipe@v1",
                "test_data/recipes/nonlocal_bad.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(
            result.returncode, 0, "Expected validation to cause failure"
        )
        stderr_lower = result.stderr.lower()
        self.assertTrue(
            "security" in stderr_lower or "local" in stderr_lower,
            f"Expected security/local dep error, got: {result.stderr}",
        )

    def test_remote_file_uri_no_dependencies(self):
        """Remote recipe with file:// source and no dependencies succeeds."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "remote.fileuri@v1",
                "test_data/recipes/remote_fileuri.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 1)

        key, value = lines[0].split(" -> ", 1)
        self.assertEqual(key, "remote.fileuri@v1")
        self.assertGreater(len(value), 0)

    def test_remote_depends_on_remote(self):
        """Remote recipe depending on another remote recipe succeeds."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "remote.parent@v1",
                "test_data/recipes/remote_parent.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 2, f"Expected 2 recipes, got: {result.stdout}")

        output = dict(line.split(" -> ", 1) for line in lines)
        self.assertIn("remote.parent@v1", output)
        self.assertIn("remote.child@v1", output)

    def test_local_depends_on_remote(self):
        """Local recipe depending on remote recipe succeeds."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.wrapper@v1",
                "test_data/recipes/local_wrapper.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 2, f"Expected 2 recipes, got: {result.stdout}")

        output = dict(line.split(" -> ", 1) for line in lines)
        self.assertIn("local.wrapper@v1", output)
        self.assertIn("remote.base@v1", output)

    def test_local_depends_on_local(self):
        """Local recipe depending on another local recipe succeeds."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.parent@v1",
                "test_data/recipes/local_parent.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 2, f"Expected 2 recipes, got: {result.stdout}")

        output = dict(line.split(" -> ", 1) for line in lines)
        self.assertIn("local.parent@v1", output)
        self.assertIn("local.child@v1", output)

    def test_transitive_local_dependency_rejected(self):
        """Remote recipe transitively depending on local.* fails."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "remote.a@v1",
                "test_data/recipes/remote_transitive_a.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(
            result.returncode, 0, "Expected transitive violation to cause failure"
        )
        stderr_lower = result.stderr.lower()
        self.assertTrue(
            "security" in stderr_lower or "local" in stderr_lower,
            f"Expected security/local dep error, got: {result.stderr}",
        )


if __name__ == "__main__":
    unittest.main()

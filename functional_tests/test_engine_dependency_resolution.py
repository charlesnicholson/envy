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

from . import test_config


class TestEngineDependencyResolution(unittest.TestCase):
    """Tests for dependency graph construction and validation."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-engine-test-"))
        self.envy_test = test_config.get_envy_executable()
        self.envy = test_config.get_envy_executable()
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
        """Engine loads spec and its dependency."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.withdep@v1",
                "test_data/specs/with_dep.lua",
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
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.cycle_a@v1",
                "test_data/specs/cycle_a.lua",
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

    def test_self_dependency_detection(self):
        """Engine detects and rejects spec depending on itself."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.self_dep@v1",
                "test_data/specs/self_dep.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(
            result.returncode, 0, "Expected self-dependency to cause failure"
        )
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
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.diamond_a@v1",
                "test_data/specs/diamond_a.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stderr: {result.stderr}\n\nstdout: {result.stdout}"
        )

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(
            len(lines), 4, f"Expected 4 specs (A,B,C,D once), got: {result.stdout}"
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

    def test_dependency_completion_blocks_parent_check(self):
        """Dependency marked needed_by=check runs to completion and graph fully resolves."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.fetch_dep_blocked@v1",
                "test_data/specs/fetch_dep_blocked.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stderr: {result.stderr}\nstdout: {result.stdout}"
        )

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(
            len(lines),
            2,
            f"Expected 2 specs (parent + helper), got: {result.stdout}",
        )

        output = dict(line.split(" -> ", 1) for line in lines)
        self.assertIn("local.fetch_dep_blocked@v1", output)
        self.assertIn("local.fetch_dep_helper@v1", output)

    def test_multiple_independent_roots(self):
        """Engine resolves multiple independent dependency trees."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.multiple_roots@v1",
                "test_data/specs/multiple_roots.lua",
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
        """Same spec identity with different options creates separate entries."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.options_parent@v1",
                "test_data/specs/options_parent.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(
            len(lines),
            3,
            f"Expected 3 specs (parent + 2 variants), got: {result.stdout}",
        )

        # Verify all present with options in keys (strings are quoted)
        output = dict(line.split(" -> ", 1) for line in lines)
        self.assertIn("local.options_parent@v1", output)
        self.assertIn('local.with_options@v1{variant="bar"}', output)
        self.assertIn('local.with_options@v1{variant="foo"}', output)

    def test_deep_chain_dependency(self):
        """Engine resolves deep dependency chain (A->B->C->D->E)."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.chain_a@v1",
                "test_data/specs/chain_a.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(
            len(lines), 5, f"Expected 5 specs (A,B,C,D,E), got: {result.stdout}"
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
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.fanout_root@v1",
                "test_data/specs/fanout_root.lua",
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
        """Engine rejects non-local spec depending on local.*."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "remote.badrecipe@v1",
                "test_data/specs/nonlocal_bad.lua",
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
        """Remote spec with file:// source and no dependencies succeeds."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "remote.fileuri@v1",
                "test_data/specs/remote_fileuri.lua",
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
        """Remote spec depending on another remote spec succeeds."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "remote.parent@v1",
                "test_data/specs/remote_parent.lua",
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
        """Local spec depending on remote spec succeeds."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.wrapper@v1",
                "test_data/specs/local_wrapper.lua",
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
        """Local spec depending on another local spec succeeds."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.parent@v1",
                "test_data/specs/local_parent.lua",
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
        """Remote spec transitively depending on local.* fails."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "remote.a@v1",
                "test_data/specs/remote_transitive_a.lua",
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

    def test_fetch_dependency_cycle(self):
        """Engine detects and rejects fetch dependency cycles."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.fetch_cycle_a@v1",
                "test_data/specs/fetch_cycle_a.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(
            result.returncode, 0, "Expected fetch dependency cycle to cause failure"
        )
        stderr_lower = result.stderr.lower()
        # Accept either "Fetch dependency cycle" or "Dependency cycle" as both indicate detection
        self.assertIn(
            "cycle",
            stderr_lower,
            f"Expected cycle error, got: {result.stderr}",
        )
        # Verify the cycle path includes both recipes
        self.assertIn(
            "fetch_cycle_a",
            stderr_lower,
            f"Expected fetch_cycle_a in error, got: {result.stderr}",
        )
        self.assertIn(
            "fetch_cycle_b",
            stderr_lower,
            f"Expected fetch_cycle_b in error, got: {result.stderr}",
        )

    def test_simple_fetch_dependency(self):
        """Simple fetch dependency: A fetch needs B - validates basic flow and blocking."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.simple_fetch_dep_parent@v1",
                "test_data/specs/simple_fetch_dep_parent.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stderr: {result.stderr}\\nstdout: {result.stdout}"
        )

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(
            len(lines),
            3,
            f"Expected 3 specs (parent + child + base), got: {result.stdout}",
        )

        output = dict(line.split(" -> ", 1) for line in lines)
        self.assertIn("local.simple_fetch_dep_parent@v1", output)
        self.assertIn("local.simple_fetch_dep_child@v1", output)
        self.assertIn("local.simple_fetch_dep_base@v1", output)

    def test_multi_level_nesting(self):
        """Multi-level nesting: A fetch needs B, B fetch needs C, C fetch needs base."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.multi_level_a@v1",
                "test_data/specs/multi_level_a.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stderr: {result.stderr}\\nstdout: {result.stdout}"
        )

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(
            len(lines),
            4,
            f"Expected 4 specs (A + B + C + base), got: {result.stdout}",
        )

        output = dict(line.split(" -> ", 1) for line in lines)
        self.assertIn("local.multi_level_a@v1", output)
        self.assertIn("local.multi_level_b@v1", output)
        self.assertIn("local.multi_level_c@v1", output)
        self.assertIn("local.simple_fetch_dep_base@v1", output)

    def test_multiple_fetch_dependencies(self):
        """Multiple fetch dependencies: A fetch needs [B, C] - parallel installation."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.multiple_fetch_deps_parent@v1",
                "test_data/specs/multiple_fetch_deps_parent.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stderr: {result.stderr}\\nstdout: {result.stdout}"
        )

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(
            len(lines),
            4,
            f"Expected 4 specs (parent + child + base + helper), got: {result.stdout}",
        )

        output = dict(line.split(" -> ", 1) for line in lines)
        self.assertIn("local.multiple_fetch_deps_parent@v1", output)
        self.assertIn("local.multiple_fetch_deps_child@v1", output)
        self.assertIn("local.simple_fetch_dep_base@v1", output)
        self.assertIn("local.fetch_dep_helper@v1", output)


if __name__ == "__main__":
    unittest.main()

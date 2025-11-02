#!/usr/bin/env python3
"""Functional tests for engine execution."""

import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest


class TestEngine(unittest.TestCase):
    """Basic engine execution tests."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-engine-test-"))
        self.envy_test = (
            Path(__file__).parent.parent / "out" / "build" / "envy_functional_tester"
        )

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)

    def test_single_local_recipe_no_deps(self):
        """Engine loads single local recipe with no dependencies."""
        result = subprocess.run(
            [
                str(self.envy_test),
                "engine-test",
                "local.simple@1.0.0",
                "test_data/recipes/simple.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Output should be single line: id_or_identity -> asset_hash
        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 1)

        key, value = lines[0].split(" -> ", 1)
        self.assertEqual(key, "local.simple@1.0.0")
        self.assertTrue(len(value) > 0)

    def test_recipe_with_one_dependency(self):
        """Engine loads recipe and its dependency."""
        result = subprocess.run(
            [
                str(self.envy_test),
                "engine-test",
                "local.withdep@1.0.0",
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
        self.assertIn("local.withdep@1.0.0", output)
        self.assertIn("local.simple@v1", output)

    def test_cycle_detection(self):
        """Engine detects and rejects dependency cycles."""
        result = subprocess.run(
            [
                str(self.envy_test),
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

    def test_validation_no_phases(self):
        """Engine rejects recipe with no phases."""
        result = subprocess.run(
            [
                str(self.envy_test),
                "engine-test",
                "local.nophases@1.0.0",
                "test_data/recipes/no_phases.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(
            result.returncode, 0, "Expected validation to cause failure"
        )
        # Error should mention missing phases
        stderr_lower = result.stderr.lower()
        self.assertTrue(
            "check" in stderr_lower
            or "install" in stderr_lower
            or "fetch" in stderr_lower,
            f"Expected phase validation error, got: {result.stderr}",
        )

    def test_diamond_dependency_memoization(self):
        """Engine memoizes shared dependencies (diamond: A->B,C; B,C->D)."""
        result = subprocess.run(
            [
                str(self.envy_test),
                "engine-test",
                "local.diamond_a@1.0.0",
                "test_data/recipes/diamond_a.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(
            len(lines), 4, f"Expected 4 recipes (A,B,C,D once), got: {result.stdout}"
        )

        # Verify all present
        output = dict(line.split(" -> ", 1) for line in lines)
        self.assertIn("local.diamond_a@1.0.0", output)
        self.assertIn("local.diamond_b@1.0.0", output)
        self.assertIn("local.diamond_c@1.0.0", output)
        self.assertIn("local.diamond_d@1.0.0", output)

    def test_multiple_independent_roots(self):
        """Engine resolves multiple independent dependency trees."""
        result = subprocess.run(
            [
                str(self.envy_test),
                "engine-test",
                "local.multiple_roots@1.0.0",
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
        self.assertIn("local.multiple_roots@1.0.0", output)
        self.assertIn("local.independent_left@1.0.0", output)
        self.assertIn("local.independent_right@1.0.0", output)

    def test_options_differentiate_recipes(self):
        """Same recipe identity with different options creates separate entries."""
        result = subprocess.run(
            [
                str(self.envy_test),
                "engine-test",
                "local.options_parent@1.0.0",
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
        self.assertIn("local.options_parent@1.0.0", output)
        self.assertIn("local.with_options@1.0.0{variant=bar}", output)
        self.assertIn("local.with_options@1.0.0{variant=foo}", output)

    def test_deep_chain_dependency(self):
        """Engine resolves deep dependency chain (A->B->C->D->E)."""
        result = subprocess.run(
            [
                str(self.envy_test),
                "engine-test",
                "local.chain_a@1.0.0",
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
        self.assertIn("local.chain_a@1.0.0", output)
        self.assertIn("local.chain_b@1.0.0", output)
        self.assertIn("local.chain_c@1.0.0", output)
        self.assertIn("local.chain_d@1.0.0", output)
        self.assertIn("local.chain_e@1.0.0", output)

    def test_wide_fanout_dependency(self):
        """Engine resolves wide fan-out (root->child1,2,3,4)."""
        result = subprocess.run(
            [
                str(self.envy_test),
                "engine-test",
                "local.fanout_root@1.0.0",
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
        self.assertIn("local.fanout_root@1.0.0", output)
        self.assertIn("local.fanout_child1@1.0.0", output)
        self.assertIn("local.fanout_child2@1.0.0", output)
        self.assertIn("local.fanout_child3@1.0.0", output)
        self.assertIn("local.fanout_child4@1.0.0", output)

    @unittest.skip("Requires remote source implementation")
    def test_nonlocal_cannot_depend_on_local(self):
        """Engine rejects non-local recipe depending on local.*."""
        result = subprocess.run(
            [
                str(self.envy_test),
                "engine-test",
                "remote.badrecipe@1.0.0",
                "test_data/recipes/nonlocal_bad.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(
            result.returncode, 0, "Expected validation to cause failure"
        )
        self.assertIn(
            "local",
            result.stderr.lower(),
            f"Expected local dep error, got: {result.stderr}",
        )


if __name__ == "__main__":
    unittest.main()

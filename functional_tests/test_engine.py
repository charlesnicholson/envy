#!/usr/bin/env python3
"""Functional tests for engine execution."""

import hashlib
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
                "local.simple@v1",
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
        self.assertEqual(key, "local.simple@v1")
        self.assertGreater(len(value), 0)

    def test_recipe_with_one_dependency(self):
        """Engine loads recipe and its dependency."""
        result = subprocess.run(
            [
                str(self.envy_test),
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
                "local.nophases@v1",
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
                "--trace",
                "engine-test",
                "local.diamond_a@v1",
                "test_data/recipes/diamond_a.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}\n\nstdout: {result.stdout}")

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

    def test_phase_execution_check_false(self):
        """Engine executes check() and install() phases with TRACE logging."""
        result = subprocess.run(
            [
                str(self.envy_test),
                "--trace",
                "engine-test",
                "local.simple@v1",
                "test_data/recipes/simple.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Verify TRACE logs show phase execution
        stderr_lower = result.stderr.lower()
        self.assertIn("check", stderr_lower, f"Expected check phase log: {result.stderr}")
        self.assertIn("install", stderr_lower, f"Expected install phase log: {result.stderr}")
        self.assertIn("local.simple@v1", stderr_lower, f"Expected identity in logs: {result.stderr}")

    def test_fetch_function_basic(self):
        """Engine executes fetch() phase for recipes with fetch function."""
        result = subprocess.run(
            [
                str(self.envy_test),
                "--trace",
                "engine-test",
                "local.fetcher@v1",
                "test_data/recipes/fetch_function_basic.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Verify TRACE logs show fetch phase execution
        stderr_lower = result.stderr.lower()
        self.assertIn("fetch", stderr_lower, f"Expected fetch phase log: {result.stderr}")
        self.assertIn("local.fetcher@v1", stderr_lower, f"Expected identity in logs: {result.stderr}")

        # Verify output contains asset hash
        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 1)
        key, value = lines[0].split(" -> ", 1)
        self.assertEqual(key, "local.fetcher@v1")
        self.assertGreater(len(value), 0)

    def test_fetch_function_with_dependency(self):
        """Engine executes fetch() with dependencies available."""
        result = subprocess.run(
            [
                str(self.envy_test),
                "--trace",
                "engine-test",
                "local.fetcher_with_dep@v1",
                "test_data/recipes/fetch_function_with_dep.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Verify both recipes executed
        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 2)

        # Verify dependency executed
        dep_lines = [l for l in lines if "local.tool@v1" in l]
        self.assertEqual(len(dep_lines), 1)

        # Verify main recipe executed
        main_lines = [l for l in lines if "local.fetcher_with_dep@v1" in l]
        self.assertEqual(len(main_lines), 1)

    def test_sha256_verification_success(self):
        """Recipe with correct SHA256 succeeds."""
        # Compute actual SHA256 of remote_child.lua
        child_recipe_path = Path(__file__).parent.parent / "test_data" / "recipes" / "remote_child.lua"
        with open(child_recipe_path, "rb") as f:
            actual_sha256 = hashlib.sha256(f.read()).hexdigest()

        # Create a temporary recipe that depends on remote_child with correct SHA256
        with tempfile.NamedTemporaryFile(mode="w", suffix=".lua", delete=False) as tmp:
            tmp.write(f"""
-- test.sha256_ok@v1
identity = "test.sha256_ok@v1"
dependencies = {{
  {{
    recipe = "remote.child@v1",
    url = "{child_recipe_path.as_posix()}",
    sha256 = "{actual_sha256}"
  }}
}}

function check(ctx)
  return false
end

function install(ctx)
  envy.info("SHA256 verification succeeded")
end
""")
            tmp_path = tmp.name

        try:
            result = subprocess.run(
                [
                    str(self.envy_test),
                    "engine-test",
                    "test.sha256_ok@v1",
                    tmp_path,
                    f"--cache-root={self.cache_root}",
                ],
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
            lines = [line for line in result.stdout.strip().split("\n") if line]
            self.assertEqual(len(lines), 2, f"Expected 2 recipes, got: {result.stdout}")
        finally:
            Path(tmp_path).unlink()

    def test_sha256_verification_failure(self):
        """Recipe with incorrect SHA256 fails."""
        child_recipe_path = Path(__file__).parent.parent / "test_data" / "recipes" / "remote_child.lua"
        wrong_sha256 = "0000000000000000000000000000000000000000000000000000000000000000"

        # Create a temporary recipe that depends on remote_child with wrong SHA256
        with tempfile.NamedTemporaryFile(mode="w", suffix=".lua", delete=False) as tmp:
            tmp.write(f"""
-- test.sha256_fail@v1
identity = "test.sha256_fail@v1"
dependencies = {{
  {{
    recipe = "remote.child@v1",
    url = "{child_recipe_path.as_posix()}",
    sha256 = "{wrong_sha256}"
  }}
}}

function check(ctx)
  return false
end

function install(ctx)
  envy.info("This should not execute")
end
""")
            tmp_path = tmp.name

        try:
            result = subprocess.run(
                [
                    str(self.envy_test),
                    "engine-test",
                    "test.sha256_fail@v1",
                    tmp_path,
                    f"--cache-root={self.cache_root}",
                ],
                capture_output=True,
                text=True,
            )

            self.assertNotEqual(result.returncode, 0, "Expected SHA256 mismatch to cause failure")
            self.assertIn("SHA256 mismatch", result.stderr, f"Expected SHA256 error, got: {result.stderr}")
            self.assertIn(wrong_sha256, result.stderr, f"Expected wrong hash in error, got: {result.stderr}")
        finally:
            Path(tmp_path).unlink()

    def test_identity_validation_correct(self):
        """Recipe with correct identity declaration succeeds."""
        result = subprocess.run(
            [
                str(self.envy_test),
                "engine-test",
                "local.identity_correct@v1",
                "test_data/recipes/identity_correct.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 1)
        self.assertIn("local.identity_correct@v1", result.stdout)

    def test_identity_validation_missing(self):
        """Recipe missing identity field fails with clear error."""
        result = subprocess.run(
            [
                str(self.envy_test),
                "engine-test",
                "local.identity_missing@v1",
                "test_data/recipes/identity_missing.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(result.returncode, 0, "Expected missing identity to cause failure")
        self.assertIn("must declare 'identity' field", result.stderr.lower(),
                     f"Expected identity field error, got: {result.stderr}")
        self.assertIn("local.identity_missing@v1", result.stderr,
                     f"Expected recipe identity in error, got: {result.stderr}")

    def test_identity_validation_mismatch(self):
        """Recipe with wrong identity fails with clear error."""
        result = subprocess.run(
            [
                str(self.envy_test),
                "engine-test",
                "local.identity_expected@v1",
                "test_data/recipes/identity_mismatch.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(result.returncode, 0, "Expected identity mismatch to cause failure")
        self.assertIn("identity mismatch", result.stderr.lower(),
                     f"Expected identity mismatch error, got: {result.stderr}")
        self.assertIn("local.identity_expected@v1", result.stderr,
                     f"Expected expected identity in error, got: {result.stderr}")
        self.assertIn("local.wrong_identity@v1", result.stderr,
                     f"Expected declared identity in error, got: {result.stderr}")

    def test_identity_validation_wrong_type(self):
        """Recipe with identity as wrong type fails with clear error."""
        result = subprocess.run(
            [
                str(self.envy_test),
                "engine-test",
                "local.identity_wrong_type@v1",
                "test_data/recipes/identity_wrong_type.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(result.returncode, 0, "Expected wrong type to cause failure")
        self.assertIn("must be a string", result.stderr.lower(),
                     f"Expected type error, got: {result.stderr}")
        self.assertIn("local.identity_wrong_type@v1", result.stderr,
                     f"Expected recipe identity in error, got: {result.stderr}")

    def test_identity_validation_local_recipe(self):
        """Local recipes also require identity validation (no exemption)."""
        # Create temp local recipe without identity
        with tempfile.NamedTemporaryFile(mode="w", suffix=".lua", delete=False) as tmp:
            tmp.write("""
-- Missing identity in local recipe
dependencies = {}
function check(ctx) return false end
function install(ctx) end
""")
            tmp_path = tmp.name

        try:
            result = subprocess.run(
                [
                    str(self.envy_test),
                    "engine-test",
                    "local.temp_no_identity@v1",
                    tmp_path,
                    f"--cache-root={self.cache_root}",
                ],
                capture_output=True,
                text=True,
            )

            self.assertNotEqual(result.returncode, 0,
                               "Expected local recipe without identity to fail")
            self.assertIn("must declare 'identity' field", result.stderr.lower(),
                         f"Expected identity field error for local recipe, got: {result.stderr}")
        finally:
            Path(tmp_path).unlink()


if __name__ == "__main__":
    unittest.main()

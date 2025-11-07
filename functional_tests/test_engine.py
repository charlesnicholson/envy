#!/usr/bin/env python3
"""Functional tests for engine execution."""

import hashlib
import os
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

    def test_single_local_recipe_no_deps(self):
        """Engine loads single local recipe with no dependencies."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
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

    def test_validation_no_phases(self):
        """Engine rejects recipe with no phases."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
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
        self.assertIn(
            "check", stderr_lower, f"Expected check phase log: {result.stderr}"
        )
        self.assertIn(
            "install", stderr_lower, f"Expected install phase log: {result.stderr}"
        )
        self.assertIn(
            "local.simple@v1",
            stderr_lower,
            f"Expected identity in logs: {result.stderr}",
        )

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
        self.assertIn(
            "fetch", stderr_lower, f"Expected fetch phase log: {result.stderr}"
        )
        self.assertIn(
            "local.fetcher@v1",
            stderr_lower,
            f"Expected identity in logs: {result.stderr}",
        )

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
        child_recipe_path = (
            Path(__file__).parent.parent / "test_data" / "recipes" / "remote_child.lua"
        )
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
                    *self.trace_flag,
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
        child_recipe_path = (
            Path(__file__).parent.parent / "test_data" / "recipes" / "remote_child.lua"
        )
        wrong_sha256 = (
            "0000000000000000000000000000000000000000000000000000000000000000"
        )

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
                    *self.trace_flag,
                    "engine-test",
                    "test.sha256_fail@v1",
                    tmp_path,
                    f"--cache-root={self.cache_root}",
                ],
                capture_output=True,
                text=True,
            )

            self.assertNotEqual(
                result.returncode, 0, "Expected SHA256 mismatch to cause failure"
            )
            self.assertIn(
                "SHA256 mismatch",
                result.stderr,
                f"Expected SHA256 error, got: {result.stderr}",
            )
            self.assertIn(
                wrong_sha256,
                result.stderr,
                f"Expected wrong hash in error, got: {result.stderr}",
            )
        finally:
            Path(tmp_path).unlink()

    def test_identity_validation_correct(self):
        """Recipe with correct identity declaration succeeds."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
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
                *self.trace_flag,
                "engine-test",
                "local.identity_missing@v1",
                "test_data/recipes/identity_missing.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(
            result.returncode, 0, "Expected missing identity to cause failure"
        )
        self.assertIn(
            "must declare 'identity' field",
            result.stderr.lower(),
            f"Expected identity field error, got: {result.stderr}",
        )
        self.assertIn(
            "local.identity_missing@v1",
            result.stderr,
            f"Expected recipe identity in error, got: {result.stderr}",
        )

    def test_identity_validation_mismatch(self):
        """Recipe with wrong identity fails with clear error."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.identity_expected@v1",
                "test_data/recipes/identity_mismatch.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(
            result.returncode, 0, "Expected identity mismatch to cause failure"
        )
        self.assertIn(
            "identity mismatch",
            result.stderr.lower(),
            f"Expected identity mismatch error, got: {result.stderr}",
        )
        self.assertIn(
            "local.identity_expected@v1",
            result.stderr,
            f"Expected expected identity in error, got: {result.stderr}",
        )
        self.assertIn(
            "local.wrong_identity@v1",
            result.stderr,
            f"Expected declared identity in error, got: {result.stderr}",
        )

    def test_identity_validation_wrong_type(self):
        """Recipe with identity as wrong type fails with clear error."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.identity_wrong_type@v1",
                "test_data/recipes/identity_wrong_type.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(
            result.returncode, 0, "Expected wrong type to cause failure"
        )
        self.assertIn(
            "must be a string",
            result.stderr.lower(),
            f"Expected type error, got: {result.stderr}",
        )
        self.assertIn(
            "local.identity_wrong_type@v1",
            result.stderr,
            f"Expected recipe identity in error, got: {result.stderr}",
        )

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
                    *self.trace_flag,
                    "engine-test",
                    "local.temp_no_identity@v1",
                    tmp_path,
                    f"--cache-root={self.cache_root}",
                ],
                capture_output=True,
                text=True,
            )

            self.assertNotEqual(
                result.returncode, 0, "Expected local recipe without identity to fail"
            )
            self.assertIn(
                "must declare 'identity' field",
                result.stderr.lower(),
                f"Expected identity field error for local recipe, got: {result.stderr}",
            )
        finally:
            Path(tmp_path).unlink()

    def test_declarative_fetch_string(self):
        """Recipe with declarative fetch (string format) downloads file."""
        result = subprocess.run(
            [
                str(self.envy_test),
                "--trace",
                "engine-test",
                "local.fetch_string@v1",
                "test_data/recipes/fetch_string.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 1)
        self.assertIn("local.fetch_string@v1", result.stdout)

        # Verify fetch phase executed
        stderr_lower = result.stderr.lower()
        self.assertIn(
            "fetch", stderr_lower, f"Expected fetch phase log: {result.stderr}"
        )

    def test_declarative_fetch_single_table(self):
        """Recipe with declarative fetch (single table with sha256) downloads and verifies."""
        # Compute hash dynamically
        simple_hash = self.get_file_hash("test_data/lua/simple.lua")

        # Create recipe with computed hash
        recipe_content = f"""-- Test declarative fetch with single table format and SHA256 verification
identity = "local.fetch_single@v1"

-- Single table format with optional sha256
fetch = {{
  url = "test_data/lua/simple.lua",
  sha256 = "{simple_hash}"
}}
"""
        modified_recipe = self.cache_root / "fetch_single.lua"
        modified_recipe.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                "--trace",
                "engine-test",
                "local.fetch_single@v1",
                str(modified_recipe),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 1)
        self.assertIn("local.fetch_single@v1", result.stdout)

        # Verify SHA256 verification occurred
        stderr_lower = result.stderr.lower()
        self.assertIn(
            "sha256", stderr_lower, f"Expected SHA256 verification log: {result.stderr}"
        )

    def test_declarative_fetch_array(self):
        """Recipe with declarative fetch (array format) downloads multiple files concurrently."""
        # Compute hashes dynamically
        simple_hash = self.get_file_hash("test_data/lua/simple.lua")
        print_single_hash = self.get_file_hash("test_data/lua/print_single.lua")

        # Create recipe with computed hashes
        recipe_content = f"""-- Test declarative fetch with array format (concurrent downloads)
identity = "local.fetch_array@v1"

-- Array format: multiple files with optional sha256
fetch = {{
  {{
    url = "test_data/lua/simple.lua",
    sha256 = "{simple_hash}"
  }},
  {{
    url = "test_data/lua/print_single.lua",
    sha256 = "{print_single_hash}"
  }},
  {{
    url = "test_data/lua/print_multiple.lua"
    -- No sha256 - should still work (permissive mode)
  }}
}}
"""
        modified_recipe = self.cache_root / "fetch_array.lua"
        modified_recipe.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                "--trace",
                "engine-test",
                "local.fetch_array@v1",
                str(modified_recipe),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 1)
        self.assertIn("local.fetch_array@v1", result.stdout)

        # Verify multiple files were downloaded
        stderr_lower = result.stderr.lower()
        self.assertIn(
            "downloading", stderr_lower, f"Expected download log: {result.stderr}"
        )
        # The log should mention "3 file(s)" or similar
        self.assertTrue(
            "3" in result.stderr or "file" in stderr_lower,
            f"Expected multiple file download log: {result.stderr}",
        )

    def test_declarative_fetch_collision(self):
        """Recipe with duplicate filenames fails with collision error."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.fetch_collision@v1",
                "test_data/recipes/fetch_collision.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(
            result.returncode, 0, "Expected filename collision to cause failure"
        )
        self.assertIn(
            "collision",
            result.stderr.lower(),
            f"Expected collision error, got: {result.stderr}",
        )
        self.assertIn(
            "simple.lua",
            result.stderr,
            f"Expected filename in error, got: {result.stderr}",
        )

    def test_declarative_fetch_bad_sha256(self):
        """Recipe with wrong SHA256 fails verification."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.fetch_bad_sha256@v1",
                "test_data/recipes/fetch_bad_sha256.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(
            result.returncode, 0, "Expected SHA256 mismatch to cause failure"
        )
        self.assertIn(
            "sha256",
            result.stderr.lower(),
            f"Expected SHA256 error, got: {result.stderr}",
        )

    def test_declarative_fetch_partial_failure_then_complete(self):
        """Partial failure caches successful files, completion reuses them (no intrusive code)."""
        # Use shared cache root so second run sees first run's cached files
        shared_cache = Path(tempfile.mkdtemp(prefix="envy-shared-cache-"))

        try:
            # Create temp directory for the missing file
            temp_dir = shared_cache / "temp_files"
            temp_dir.mkdir(parents=True, exist_ok=True)
            missing_file = temp_dir / "fetch_partial_missing.lua"

            # Compute hashes dynamically
            simple_hash = self.get_file_hash("test_data/lua/simple.lua")
            print_single_hash = self.get_file_hash("test_data/lua/print_single.lua")

            # Create empty file and compute its hash
            empty_temp = shared_cache / "empty_temp.txt"
            empty_temp.write_text("")
            empty_hash = self.get_file_hash(empty_temp)

            # Create recipe with computed hashes
            recipe_content = f"""-- Test per-file caching across partial failures
-- Two files succeed, one fails, then completion reuses cached files
identity = "local.fetch_partial@v1"

fetch = {{
  {{
    url = "test_data/lua/simple.lua",
    sha256 = "{simple_hash}"
  }},
  {{
    url = "test_data/lua/print_single.lua",
    sha256 = "{print_single_hash}"
  }},
  {{
    -- This file will be created by the test after first run
    url = "file://{temp_dir}/fetch_partial_missing.lua",
    sha256 = "{empty_hash}"
  }}
}}
"""
            modified_recipe = shared_cache / "fetch_partial_modified.lua"
            modified_recipe.write_text(recipe_content)

            # Run 1: Partial failure (2 succeed, 1 fails - missing file doesn't exist yet)
            result1 = subprocess.run(
                [
                    str(self.envy_test),
                    "--trace",
                    "engine-test",
                    "local.fetch_partial@v1",
                    str(modified_recipe),
                    f"--cache-root={shared_cache}",
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result1.returncode, 0, "Expected partial failure")
            self.assertIn(
                "fetch failed",
                result1.stderr.lower(),
                f"Expected fetch failure: {result1.stderr}",
            )

            # Verify fetch_dir has the 2 successful files (in asset cache, not recipe cache)
            # With hierarchical structure, find variant dirs under identity dir
            identity_dir = shared_cache / "assets" / "local.fetch_partial@v1"
            self.assertTrue(identity_dir.exists(), f"Identity dir should exist: {identity_dir}")
            variant_dirs = list(identity_dir.glob("*-sha256-*"))
            self.assertEqual(
                len(variant_dirs), 1, f"Expected 1 variant dir, found: {variant_dirs}"
            )
            fetch_dir = variant_dirs[0] / "fetch"
            self.assertTrue(
                (fetch_dir / "simple.lua").exists(), "simple.lua should be cached"
            )
            self.assertTrue(
                (fetch_dir / "print_single.lua").exists(),
                "print_single.lua should be cached",
            )

            # Create the missing file (empty file matches the SHA256 for empty content)
            missing_file.write_text("")

            # Run 2: Completion with cache - use same cache root
            result2 = subprocess.run(
                [
                    str(self.envy_test),
                    "--trace",
                    "engine-test",
                    "local.fetch_partial@v1",
                    str(modified_recipe),
                    f"--cache-root={shared_cache}",
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                result2.returncode, 0, f"Second run failed: {result2.stderr}"
            )

            # Verify cache hits in trace log
            stderr_lower = result2.stderr.lower()
            self.assertIn(
                "cache hit", stderr_lower, f"Expected cache hit log: {result2.stderr}"
            )
            # Should only download 1 file (the missing one)
            self.assertIn(
                "downloading 1 file(s)",
                result2.stderr,
                f"Expected 1 download: {result2.stderr}",
            )
        finally:
            shutil.rmtree(shared_cache, ignore_errors=True)

    def test_declarative_fetch_intrusive_partial_failure(self):
        """Use --fail-after-fetch-count to simulate partial download (intrusive flag)."""
        # Use shared cache root so second run sees first run's cached files
        shared_cache = Path(tempfile.mkdtemp(prefix="envy-intrusive-cache-"))

        try:
            # Compute hashes dynamically
            simple_hash = self.get_file_hash("test_data/lua/simple.lua")
            print_single_hash = self.get_file_hash("test_data/lua/print_single.lua")

            # Create recipe with computed hashes
            recipe_content = f"""-- Test declarative fetch with array format (concurrent downloads)
identity = "local.fetch_array@v1"

-- Array format: multiple files with optional sha256
fetch = {{
  {{
    url = "test_data/lua/simple.lua",
    sha256 = "{simple_hash}"
  }},
  {{
    url = "test_data/lua/print_single.lua",
    sha256 = "{print_single_hash}"
  }},
  {{
    url = "test_data/lua/print_multiple.lua"
    -- No sha256 - should still work (permissive mode)
  }}
}}
"""
            modified_recipe = shared_cache / "fetch_array.lua"
            modified_recipe.write_text(recipe_content)

            # Run 1: Download 2 files then fail
            result1 = subprocess.run(
                [
                    str(self.envy_test),
                    "--trace",
                    "engine-test",
                    "local.fetch_array@v1",  # Has 3 files
                    str(modified_recipe),
                    f"--cache-root={shared_cache}",
                    "--fail-after-fetch-count=2",
                ],
                capture_output=True,
                text=True,
            )

            self.assertNotEqual(
                result1.returncode, 0, "Expected failure after 2 downloads"
            )
            self.assertIn(
                "fail_after_fetch_count",
                result1.stderr.lower(),
                f"Expected fail_after_fetch_count error: {result1.stderr}",
            )

            # Verify fetch_dir has the first 2 files cached (in asset cache, not recipe cache)
            # With hierarchical structure, find variant dirs under identity dir
            identity_dir = shared_cache / "assets" / "local.fetch_array@v1"
            self.assertTrue(identity_dir.exists(), f"Identity dir should exist: {identity_dir}")
            variant_dirs = list(identity_dir.glob("*-sha256-*"))
            self.assertEqual(
                len(variant_dirs), 1, f"Expected 1 variant dir, found: {variant_dirs}"
            )
            fetch_dir = variant_dirs[0] / "fetch"
            # Check that at least some files exist (order may vary due to concurrent downloads)
            cached_files = list(fetch_dir.glob("*.lua")) if fetch_dir.exists() else []
            self.assertGreaterEqual(
                len(cached_files),
                2,
                f"Expected at least 2 cached files, got: {cached_files}",
            )

            # Run 2: Complete without flag - use same cache root
            result2 = subprocess.run(
                [
                    str(self.envy_test),
                    "--trace",
                    "engine-test",
                    "local.fetch_array@v1",
                    str(modified_recipe),
                    f"--cache-root={shared_cache}",
                ],
                capture_output=True,
                text=True,
            )

            self.assertEqual(
                result2.returncode, 0, f"Second run failed: {result2.stderr}"
            )

            # Verify cache hits for some files
            stderr_lower = result2.stderr.lower()
            self.assertIn(
                "cache hit", stderr_lower, f"Expected cache hit log: {result2.stderr}"
            )
            # Verify only remaining file(s) downloaded
            self.assertIn(
                "downloading 1 file(s)",
                result2.stderr,
                f"Expected 1 download in second run: {result2.stderr}",
            )
        finally:
            shutil.rmtree(shared_cache, ignore_errors=True)

    def test_declarative_fetch_corrupted_cache(self):
        """Corrupted files in fetch/ are detected and re-downloaded."""
        shared_cache = Path(tempfile.mkdtemp(prefix="envy-corrupted-cache-"))

        try:
            # Compute hashes dynamically
            simple_hash = self.get_file_hash("test_data/lua/simple.lua")
            print_single_hash = self.get_file_hash("test_data/lua/print_single.lua")

            # Create recipe with computed hashes
            recipe_content = f"""-- Test declarative fetch with array format (concurrent downloads)
identity = "local.fetch_array@v1"

-- Array format: multiple files with optional sha256
fetch = {{
  {{
    url = "test_data/lua/simple.lua",
    sha256 = "{simple_hash}"
  }},
  {{
    url = "test_data/lua/print_single.lua",
    sha256 = "{print_single_hash}"
  }},
  {{
    url = "test_data/lua/print_multiple.lua"
    -- No sha256 - should still work (permissive mode)
  }}
}}
"""
            modified_recipe = shared_cache / "fetch_array.lua"
            modified_recipe.write_text(recipe_content)

            identity_dir = shared_cache / "assets" / "local.fetch_array@v1"

            # Run 1: Let it create the structure but fail it
            result_setup = subprocess.run(
                [
                    str(self.envy_test),
                    "--trace",
                    "engine-test",
                    "local.fetch_array@v1",
                    str(modified_recipe),
                    f"--cache-root={shared_cache}",
                    "--fail-after-fetch-count=1",  # Fail after 1 file
                ],
                capture_output=True,
                text=True,
            )
            # Should fail
            self.assertNotEqual(result_setup.returncode, 0)

            # Now find the fetch directory and corrupt one of the files
            variant_dirs = list(identity_dir.glob("*-sha256-*"))
            self.assertEqual(len(variant_dirs), 1, f"Expected 1 variant dir: {variant_dirs}")
            fetch_dir = variant_dirs[0] / "fetch"

            # Corrupt simple.lua (replace with garbage that won't match SHA256)
            corrupted_file = fetch_dir / "simple.lua"
            corrupted_file.write_text("GARBAGE CONTENT THAT WILL FAIL SHA256 VERIFICATION")

            # Run 2: Should detect corruption and re-download
            result = subprocess.run(
                [
                    str(self.envy_test),
                    "--trace",
                    "engine-test",
                    "local.fetch_array@v1",
                    str(modified_recipe),
                    f"--cache-root={shared_cache}",
                ],
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 0, f"Should succeed: {result.stderr}")

            # Verify that corruption was detected and file re-downloaded
            stderr_lower = result.stderr.lower()
            self.assertIn("cache mismatch", stderr_lower,
                         f"Expected cache mismatch detection: {result.stderr}")

            # Verify asset completed successfully (entry-level marker exists)
            # Note: fetch/ is deleted after successful asset completion
            entry_complete = variant_dirs[0] / "envy-complete"
            self.assertTrue(entry_complete.exists(),
                          "Entry-level completion marker should exist after successful asset install")

            # Verify asset directory exists with installed files
            asset_dir = variant_dirs[0] / "asset"
            self.assertTrue(asset_dir.exists(), "Asset directory should exist after completion")

        finally:
            shutil.rmtree(shared_cache, ignore_errors=True)

    def test_declarative_fetch_complete_but_unmarked(self):
        """All files present with correct SHA256, but no completion marker."""
        shared_cache = Path(tempfile.mkdtemp(prefix="envy-complete-unmarked-"))

        try:
            # Compute hashes dynamically
            simple_hash = self.get_file_hash("test_data/lua/simple.lua")
            print_single_hash = self.get_file_hash("test_data/lua/print_single.lua")

            # Create recipe with computed hashes
            recipe_content = f"""-- Test declarative fetch with array format (concurrent downloads)
identity = "local.fetch_array@v1"

-- Array format: multiple files with optional sha256
fetch = {{
  {{
    url = "test_data/lua/simple.lua",
    sha256 = "{simple_hash}"
  }},
  {{
    url = "test_data/lua/print_single.lua",
    sha256 = "{print_single_hash}"
  }},
  {{
    url = "test_data/lua/print_multiple.lua"
    -- No sha256 - should still work (permissive mode)
  }}
}}
"""
            modified_recipe = shared_cache / "fetch_array.lua"
            modified_recipe.write_text(recipe_content)

            identity_dir = shared_cache / "assets" / "local.fetch_array@v1"

            # Run once to establish cache structure, then fail it
            result_setup = subprocess.run(
                [
                    str(self.envy_test),
                    "--trace",
                    "engine-test",
                    "local.fetch_array@v1",
                    str(modified_recipe),
                    f"--cache-root={shared_cache}",
                    "--fail-after-fetch-count=1",
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result_setup.returncode, 0)

            # Find fetch directory
            variant_dirs = list(identity_dir.glob("*-sha256-*"))
            self.assertEqual(len(variant_dirs), 1)
            fetch_dir = variant_dirs[0] / "fetch"
            fetch_dir.mkdir(parents=True, exist_ok=True)

            # Copy actual test files to cache (they'll match the computed hashes)
            shutil.copy("test_data/lua/simple.lua", fetch_dir / "simple.lua")
            shutil.copy("test_data/lua/print_single.lua", fetch_dir / "print_single.lua")
            shutil.copy("test_data/lua/print_multiple.lua", fetch_dir / "print_multiple.lua")

            # Ensure NO completion marker exists
            completion_marker = fetch_dir / "envy-complete"
            if completion_marker.exists():
                completion_marker.unlink()

            # Run: Should verify cached files by SHA256, reuse them
            result = subprocess.run(
                [
                    str(self.envy_test),
                    "--trace",
                    "engine-test",
                    "local.fetch_array@v1",
                    str(modified_recipe),
                    f"--cache-root={shared_cache}",
                ],
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 0, f"Should succeed: {result.stderr}")

            stderr_lower = result.stderr.lower()

            # Should see cache hits for files with SHA256
            self.assertIn("cache hit", stderr_lower,
                         f"Expected cache hits for verified files: {result.stderr}")

            # Should still download print_multiple.lua (no SHA256 = can't trust)
            self.assertIn("downloading", stderr_lower,
                         f"Expected download for file without SHA256: {result.stderr}")

            # Verify asset completed successfully (entry-level marker exists)
            # Note: fetch/ and its marker are deleted after successful asset completion
            entry_complete = variant_dirs[0] / "envy-complete"
            self.assertTrue(entry_complete.exists(),
                          "Entry-level completion marker should exist after successful asset install")

        finally:
            shutil.rmtree(shared_cache, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()

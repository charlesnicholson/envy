#!/usr/bin/env python3
"""Functional tests for engine recipe loading and validation.

Tests the recipe fetch phase: loading recipes, validating identity field,
verifying recipe SHA256, and checking basic structure requirements.
"""

import hashlib
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest

from . import test_config


class TestEngineRecipeLoading(unittest.TestCase):
    """Tests for recipe loading and validation phase."""

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
IDENTITY = "test.sha256_ok@v1"
DEPENDENCIES = {{
  {{
    recipe = "remote.child@v1",
    source = "{child_recipe_path.as_posix()}",
    sha256 = "{actual_sha256}"
  }}
}}

function CHECK(ctx)
  return false
end

function INSTALL(ctx, opts)
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
IDENTITY = "test.sha256_fail@v1"
DEPENDENCIES = {{
  {{
    recipe = "remote.child@v1",
    source = "{child_recipe_path.as_posix()}",
    sha256 = "{wrong_sha256}"
  }}
}}

function CHECK(ctx)
  return false
end

function INSTALL(ctx, opts)
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
            "must define 'identity' global as a string",
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
            "must define 'identity' global as a string",
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
DEPENDENCIES = {}
function CHECK(ctx) return false end
function INSTALL(ctx, opts) end
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
                "must define 'identity' global as a string",
                result.stderr.lower(),
                f"Expected identity field error for local recipe, got: {result.stderr}",
            )
        finally:
            Path(tmp_path).unlink()


if __name__ == "__main__":
    unittest.main()

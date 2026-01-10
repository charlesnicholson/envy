#!/usr/bin/env python3
"""Functional tests for engine spec loading and validation.

Tests the spec fetch phase: loading recipes, validating identity field,
verifying spec SHA256, and checking basic structure requirements.
"""

import hashlib
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest

from . import test_config

# Inline specs for spec loading tests
SPECS = {
    "simple.lua": """-- Minimal test spec - no dependencies
IDENTITY = "local.simple@v1"
DEPENDENCIES = {}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package - no cache interaction
end
""",
    "no_phases.lua": """-- Invalid spec - no phases defined
IDENTITY = "local.nophases@v1"
DEPENDENCIES = {}
""",
    "remote_child.lua": """-- remote.child@v1
-- Remote spec with no dependencies

IDENTITY = "remote.child@v1"

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.info("Installing remote child recipe")
end
""",
    "identity_correct.lua": """-- Spec with correct identity declaration (valid)
IDENTITY = "local.identity_correct@v1"
DEPENDENCIES = {}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.info("Identity validation passed")
end
""",
    "identity_mismatch.lua": """-- Spec with wrong identity declaration (mismatch)
IDENTITY = "local.wrong_identity@v1"
DEPENDENCIES = {}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- This should never execute
end
""",
    "identity_missing.lua": """-- Spec missing identity declaration (invalid)
DEPENDENCIES = {}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- This should never execute
end
""",
    "identity_wrong_type.lua": """-- Spec with identity as wrong type (table instead of string)
IDENTITY = { name = "wrong" }
DEPENDENCIES = {}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- This should never execute
end
""",
    "validate_ok.lua": """IDENTITY = "test.validate_ok@v1"

VALIDATE = function(opts)
  if opts and opts.foo then
    assert(opts.foo == "bar")
  end
end

CHECK = function(project_root, options) return true end
INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
""",
    "validate_false.lua": """IDENTITY = "test.validate_false@v1"

VALIDATE = function(opts)
  return false
end

CHECK = function(project_root, options) return true end
INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
""",
    "validate_string.lua": """IDENTITY = "test.validate_string@v1"

VALIDATE = function(opts)
  return "nope"
end

CHECK = function(project_root, options) return true end
INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
""",
    "validate_type.lua": """IDENTITY = "test.validate_type@v1"

VALIDATE = function(opts)
  return 123
end

CHECK = function(project_root, options) return true end
INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
""",
    "validate_nonfn.lua": """IDENTITY = "test.validate_nonfn@v1"

VALIDATE = 42

CHECK = function(project_root, options) return true end
INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
""",
    "validate_error.lua": """IDENTITY = "test.validate_error@v1"

VALIDATE = function(opts)
  error("boom")
end

CHECK = function(project_root, options) return true end
INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
""",
}


class TestEngineSpecLoading(unittest.TestCase):
    """Tests for spec loading and validation phase."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-engine-test-"))
        self.specs_dir = Path(tempfile.mkdtemp(prefix="envy-engine-specs-"))
        self.envy_test = test_config.get_envy_executable()
        self.envy = test_config.get_envy_executable()
        # Enable trace for all tests if ENVY_TEST_TRACE is set
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

        # Write inline specs to temp directory
        for name, content in SPECS.items():
            (self.specs_dir / name).write_text(content, encoding="utf-8")

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.specs_dir, ignore_errors=True)

    def get_file_hash(self, filepath):
        """Get SHA256 hash of file using envy hash command."""
        result = subprocess.run(
            [str(self.envy), "hash", str(filepath)],
            capture_output=True,
            text=True,
            check=True,
        )
        return result.stdout.strip()

    def test_single_local_spec_no_deps(self):
        """Engine loads single local spec with no dependencies."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.simple@v1",
                str(self.specs_dir / "simple.lua"),
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
        """Engine rejects spec with no phases."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.nophases@v1",
                str(self.specs_dir / "no_phases.lua"),
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
        """Spec with correct SHA256 succeeds."""
        # Compute actual SHA256 of remote_child.lua
        child_spec_path = self.specs_dir / "remote_child.lua"
        with open(child_spec_path, "rb") as f:
            actual_sha256 = hashlib.sha256(f.read()).hexdigest()

        # Create a temporary spec that depends on remote_child with correct SHA256
        with tempfile.NamedTemporaryFile(mode="w", suffix=".lua", delete=False) as tmp:
            tmp.write(f"""
-- test.sha256_ok@v1
IDENTITY = "test.sha256_ok@v1"
DEPENDENCIES = {{
  {{
    spec = "remote.child@v1",
    source = "{child_spec_path.as_posix()}",
    sha256 = "{actual_sha256}"
  }}
}}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.info("SHA256 verification succeeded")
end
""")
            tmp_path = tmp.name

        try:
            result = subprocess.run(
                [
                    str(self.envy_test),
                    f"--cache-root={self.cache_root}",
                    *self.trace_flag,
                    "engine-test",
                    "test.sha256_ok@v1",
                    tmp_path,
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
        """Spec with incorrect SHA256 fails."""
        child_spec_path = self.specs_dir / "remote_child.lua"
        wrong_sha256 = (
            "0000000000000000000000000000000000000000000000000000000000000000"
        )

        # Create a temporary spec that depends on remote_child with wrong SHA256
        with tempfile.NamedTemporaryFile(mode="w", suffix=".lua", delete=False) as tmp:
            tmp.write(f"""
-- test.sha256_fail@v1
IDENTITY = "test.sha256_fail@v1"
DEPENDENCIES = {{
  {{
    spec = "remote.child@v1",
    source = "{child_spec_path.as_posix()}",
    sha256 = "{wrong_sha256}"
  }}
}}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.info("This should not execute")
end
""")
            tmp_path = tmp.name

        try:
            result = subprocess.run(
                [
                    str(self.envy_test),
                    f"--cache-root={self.cache_root}",
                    *self.trace_flag,
                    "engine-test",
                    "test.sha256_fail@v1",
                    tmp_path,
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
        """Spec with correct identity declaration succeeds."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.identity_correct@v1",
                str(self.specs_dir / "identity_correct.lua"),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 1)
        self.assertIn("local.identity_correct@v1", result.stdout)

    def test_identity_validation_missing(self):
        """Spec missing identity field fails with clear error."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.identity_missing@v1",
                str(self.specs_dir / "identity_missing.lua"),
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
            f"Expected spec identity in error, got: {result.stderr}",
        )

    def test_identity_validation_mismatch(self):
        """Spec with wrong identity fails with clear error."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.identity_expected@v1",
                str(self.specs_dir / "identity_mismatch.lua"),
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
        """Spec with identity as wrong type fails with clear error."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.identity_wrong_type@v1",
                str(self.specs_dir / "identity_wrong_type.lua"),
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
            f"Expected spec identity in error, got: {result.stderr}",
        )

    def test_identity_validation_local_spec(self):
        """Local specs also require identity validation (no exemption)."""
        # Create temp local spec without identity
        with tempfile.NamedTemporaryFile(mode="w", suffix=".lua", delete=False) as tmp:
            tmp.write("""
-- Missing identity in local spec
DEPENDENCIES = {}
function CHECK(project_root, options) return false end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
""")
            tmp_path = tmp.name

        try:
            result = subprocess.run(
                [
                    str(self.envy_test),
                    f"--cache-root={self.cache_root}",
                    *self.trace_flag,
                    "engine-test",
                    "local.temp_no_identity@v1",
                    tmp_path,
                ],
                capture_output=True,
                text=True,
            )

            self.assertNotEqual(
                result.returncode, 0, "Expected local spec without identity to fail"
            )
            self.assertIn(
                "must define 'identity' global as a string",
                result.stderr.lower(),
                f"Expected identity field error for local spec, got: {result.stderr}",
            )
        finally:
            Path(tmp_path).unlink()

    def test_validate_hook_success(self):
        """VALIDATE nil/true succeeds."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "test.validate_ok@v1",
                str(self.specs_dir / "validate_ok.lua"),
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_validate_hook_false(self):
        """VALIDATE returning false fails."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "test.validate_false@v1",
                str(self.specs_dir / "validate_false.lua"),
            ],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("returned false", result.stderr)

    def test_validate_hook_string(self):
        """VALIDATE returning string surfaces message."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "test.validate_string@v1",
                str(self.specs_dir / "validate_string.lua"),
            ],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("nope", result.stderr)

    def test_validate_hook_invalid_return(self):
        """VALIDATE returning invalid type errors."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "test.validate_type@v1",
                str(self.specs_dir / "validate_type.lua"),
            ],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("VALIDATE must return", result.stderr)

    def test_validate_hook_non_function(self):
        """VALIDATE non-function errors."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "test.validate_nonfn@v1",
                str(self.specs_dir / "validate_nonfn.lua"),
            ],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("VALIDATE must be a function", result.stderr)

    def test_validate_hook_runtime_error(self):
        """VALIDATE error bubbles with context."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "test.validate_error@v1",
                str(self.specs_dir / "validate_error.lua"),
            ],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("boom", result.stderr)

    def test_spec_source_not_found_in_manifest(self):
        """Missing spec source from manifest entry gives clear error."""
        # Create manifest referencing non-existent source
        with tempfile.NamedTemporaryFile(
            mode="w", suffix=".lua", delete=False, dir=self.cache_root
        ) as tmp:
            tmp.write("""
-- @envy version "0.0.0"
-- @envy bin-dir "tools"
PACKAGES = {
  { spec = "local.missing@v1", source = "nonexistent_source.lua" }
}
""")
            manifest_path = tmp.name

        try:
            result = subprocess.run(
                [
                    str(self.envy),
                    f"--cache-root={self.cache_root}",
                    *self.trace_flag,
                    "sync",
                    "--install-all",
                    "--manifest",
                    manifest_path,
                ],
                capture_output=True,
                text=True,
            )

            self.assertNotEqual(
                result.returncode, 0, "Expected missing source file to cause failure"
            )
            self.assertIn(
                "Spec source not found",
                result.stderr,
                f"Expected 'Spec source not found' error, got: {result.stderr}",
            )
            self.assertIn(
                "nonexistent_source.lua",
                result.stderr,
                f"Expected source path in error, got: {result.stderr}",
            )
            self.assertIn(
                "local.missing@v1",
                result.stderr,
                f"Expected spec identity in error, got: {result.stderr}",
            )
        finally:
            Path(manifest_path).unlink()


if __name__ == "__main__":
    unittest.main()

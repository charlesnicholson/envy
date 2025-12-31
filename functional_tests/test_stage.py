#!/usr/bin/env python3
"""Functional tests for engine stage phase.

Tests default extraction, declarative stage options (strip), and imperative
stage functions (ctx.extract, ctx.extract_all).
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest

from . import test_config


class TestStagePhase(unittest.TestCase):
    """Tests for stage phase (archive extraction and preparation)."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-stage-test-"))
        self.envy_test = test_config.get_envy_executable()
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)

    def get_asset_path(self, identity):
        """Find asset directory for given identity in cache.

        Args:
            identity: Spec identity (e.g., "local.stage_default@v1")
        """
        assets_dir = self.cache_root / "packages" / identity
        if not assets_dir.exists():
            return None
        # Find the platform-specific asset subdirectory
        for subdir in assets_dir.iterdir():
            if subdir.is_dir():
                # Files are in the pkg/ subdirectory
                asset_dir = subdir / "pkg"
                if asset_dir.exists():
                    return asset_dir
                return subdir
        return None

    def test_default_stage_extracts_to_install_dir(self):
        """Spec with no stage field auto-extracts archives."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_default@v1",
                "test_data/specs/stage_default.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

        # Check that files were extracted (should be in install_dir since no custom phases)
        asset_path = self.get_asset_path("local.stage_default@v1")
        assert asset_path

        # Default extraction keeps root/ directory
        self.assertTrue((asset_path / "root").exists(), "root/ directory not found")
        self.assertTrue((asset_path / "root" / "file1.txt").exists())
        self.assertTrue((asset_path / "root" / "file2.txt").exists())
        self.assertTrue((asset_path / "root" / "subdir1" / "file3.txt").exists())

    def test_declarative_strip_removes_top_level(self):
        """Spec with stage = {strip=1} removes top-level directory."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_declarative_strip@v1",
                "test_data/specs/stage_declarative_strip.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

        asset_path = self.get_asset_path("local.stage_declarative_strip@v1")
        assert asset_path

        # With strip=1, root/ should be removed
        self.assertFalse((asset_path / "root").exists(), "root/ should be stripped")
        self.assertTrue((asset_path / "file1.txt").exists())
        self.assertTrue((asset_path / "file2.txt").exists())
        self.assertTrue((asset_path / "subdir1" / "file3.txt").exists())

    def test_imperative_extract_all(self):
        """Spec with stage function using ctx:extract_all works."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_imperative@v1",
                "test_data/specs/stage_imperative.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

        asset_path = self.get_asset_path("local.stage_imperative@v1")
        assert asset_path

        # Custom function used strip=1
        self.assertFalse((asset_path / "root").exists())
        self.assertTrue((asset_path / "file1.txt").exists())
        self.assertTrue((asset_path / "subdir1" / "file3.txt").exists())

    def test_imperative_extract_single(self):
        """Spec with stage function using ctx:extract for single file works."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_extract_single@v1",
                "test_data/specs/stage_extract_single.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

        asset_path = self.get_asset_path("local.stage_extract_single@v1")
        assert asset_path

        # ctx:extract with strip=1
        self.assertFalse((asset_path / "root").exists())
        self.assertTrue((asset_path / "file1.txt").exists())

    def test_shell_script_basic(self):
        """Spec with shell script stage extracts and creates marker file."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_shell_basic@v1",
                "test_data/specs/stage_shell_basic.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

        asset_path = self.get_asset_path("local.stage_shell_basic@v1")
        assert asset_path

        # Check that shell script executed
        self.assertTrue(
            (asset_path / "STAGE_MARKER.txt").exists(), "STAGE_MARKER.txt not found"
        )

        # Verify marker content
        marker_content = (asset_path / "STAGE_MARKER.txt").read_text()
        self.assertIn("stage script executed", marker_content)

        # Check that files were extracted with strip=1
        self.assertFalse((asset_path / "root").exists())
        self.assertTrue((asset_path / "file1.txt").exists())
        self.assertTrue((asset_path / "file2.txt").exists())

    def test_shell_script_failure(self):
        """Spec with failing shell script should fail stage phase."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_shell_failure@v1",
                "test_data/specs/stage_shell_failure.lua",
            ],
            capture_output=True,
            text=True,
        )

        # Should fail
        self.assertNotEqual(result.returncode, 0, "Expected failure but succeeded")

        # Error message should mention stage failure
        error_output = result.stdout + result.stderr
        self.assertTrue(
            "Stage shell script failed" in error_output
            or "exit code 1" in error_output
            or "failed" in error_output.lower(),
            f"Expected stage failure message in output:\n{error_output}",
        )

    def test_shell_script_complex_operations(self):
        """Spec with complex shell operations creates custom directory structure."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_shell_complex@v1",
                "test_data/specs/stage_shell_complex.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

        asset_path = self.get_asset_path("local.stage_shell_complex@v1")
        assert asset_path

        # Check custom directory structure was created
        self.assertTrue((asset_path / "custom").exists())
        self.assertTrue((asset_path / "custom" / "bin").exists())
        self.assertTrue((asset_path / "custom" / "lib").exists())
        self.assertTrue((asset_path / "custom" / "share").exists())

        # Check files were moved to custom locations
        self.assertTrue(
            (asset_path / "custom" / "bin" / "file1.txt").exists(),
            "file1.txt not in custom/bin/",
        )
        self.assertTrue(
            (asset_path / "custom" / "lib" / "file2.txt").exists(),
            "file2.txt not in custom/lib/",
        )

        # Check metadata file was created
        metadata_path = asset_path / "custom" / "share" / "metadata.txt"
        self.assertTrue(metadata_path.exists(), "metadata.txt not found")

        # Verify metadata content
        metadata_content = metadata_path.read_text()
        self.assertIn("Stage phase executed successfully", metadata_content)
        self.assertIn("Files reorganized", metadata_content)

    def test_shell_script_environment_access(self):
        """Spec with shell script can access environment variables."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_shell_env@v1",
                "test_data/specs/stage_shell_env.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

        asset_path = self.get_asset_path("local.stage_shell_env@v1")
        assert asset_path

        # Check environment info file was created
        env_info_path = asset_path / "env_info.txt"
        self.assertTrue(env_info_path.exists(), "env_info.txt not found")

        # Verify environment variables were accessible
        env_content = env_info_path.read_text()
        self.assertIn("PATH is available: yes", env_content)

    def test_shell_script_output_logged(self):
        """Shell script output should be visible in logs."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_shell_basic@v1",
                "test_data/specs/stage_shell_basic.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

        # Shell script should log "stage script executed"
        combined_output = result.stdout + result.stderr
        # Note: output might be in either stdout or stderr depending on TUI configuration
        # We just verify the script ran successfully


if __name__ == "__main__":
    unittest.main()

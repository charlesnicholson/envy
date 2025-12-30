#!/usr/bin/env python3
"""Functional tests for engine build phase.

Tests build phase with nil, string, and function forms. Verifies ctx API
(run, asset, copy, move, extract) and build phase integration.
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest

from . import test_config


class TestBuildPhase(unittest.TestCase):
    """Tests for build phase (compilation and processing workflows)."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-build-test-"))
        self.envy_test = test_config.get_envy_executable()
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)

    def get_asset_path(self, identity):
        """Find asset directory for given identity in cache."""
        assets_dir = self.cache_root / "packages" / identity
        if not assets_dir.exists():
            return None
        # Find the platform-specific asset subdirectory
        for subdir in assets_dir.iterdir():
            if subdir.is_dir():
                asset_dir = subdir / "pkg"
                if asset_dir.exists():
                    return asset_dir
                return subdir
        return None

    def run_recipe(self, recipe_file, identity, should_succeed=True):
        """Helper to run a recipe and return result."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                identity,
                f"test_data/specs/{recipe_file}",
            ],
            capture_output=True,
            text=True,
        )

        if should_succeed:
            self.assertEqual(
                result.returncode,
                0,
                f"Recipe {recipe_file} failed:\nstdout: {result.stdout}\nstderr: {result.stderr}",
            )
        else:
            self.assertNotEqual(
                result.returncode,
                0,
                f"Recipe {recipe_file} should have failed but succeeded",
            )

        return result

    def test_build_nil_skips_phase(self):
        """Recipe with no build field should skip build phase."""
        self.run_recipe("build_nil.lua", "local.build_nil@v1")

        # Files should still be present from stage
        asset_path = self.get_asset_path("local.build_nil@v1")
        self.assertIsNotNone(asset_path)
        self.assertTrue((asset_path / "file1.txt").exists())

    def test_build_string_executes_shell(self):
        """Recipe with build = "string" executes shell script."""
        self.run_recipe("build_string.lua", "local.build_string@v1")

        # Verify build artifacts were created
        asset_path = self.get_asset_path("local.build_string@v1")
        self.assertIsNotNone(asset_path)
        self.assertTrue((asset_path / "build_output").exists())
        self.assertTrue((asset_path / "build_output" / "artifact.txt").exists())

        # Verify artifact content
        content = (asset_path / "build_output" / "artifact.txt").read_text()
        self.assertEqual(content.strip(), "build_artifact")

    def test_build_function_with_ctx_run(self):
        """Recipe with build = function(ctx) uses envy.run()."""
        self.run_recipe("build_function.lua", "local.build_function@v1")

        # Verify build artifacts
        asset_path = self.get_asset_path("local.build_function@v1")
        self.assertIsNotNone(asset_path)
        self.assertTrue((asset_path / "build_output" / "result.txt").exists())

        content = (asset_path / "build_output" / "result.txt").read_text()
        self.assertEqual(content.strip(), "function_artifact")

    def test_build_with_asset_dependency(self):
        """Build phase can access dependencies via envy.asset()."""
        # Build recipe with dependency (engine will build dependency automatically)
        self.run_recipe("build_with_asset.lua", "local.build_with_asset@v1")

        # Verify dependency was used
        asset_path = self.get_asset_path("local.build_with_asset@v1")
        self.assertIsNotNone(asset_path)
        self.assertTrue((asset_path / "from_dependency.txt").exists())

        content = (asset_path / "from_dependency.txt").read_text()
        self.assertEqual(content.strip(), "dependency_data")

    def test_build_with_copy_operations(self):
        """Build phase can copy files and directories with envy.copy()."""
        self.run_recipe("build_with_copy.lua", "local.build_with_copy@v1")

        # Verify copies
        asset_path = self.get_asset_path("local.build_with_copy@v1")
        self.assertIsNotNone(asset_path)

        # Verify file copy
        self.assertTrue((asset_path / "dest_file.txt").exists())
        content = (asset_path / "dest_file.txt").read_text()
        self.assertEqual(content.strip(), "source_file")

        # Verify directory copy
        self.assertTrue((asset_path / "dest_dir").is_dir())
        self.assertTrue((asset_path / "dest_dir" / "file1.txt").exists())
        self.assertTrue((asset_path / "dest_dir" / "file2.txt").exists())

    def test_build_with_move_operations(self):
        """Build phase can move files and directories with envy.move()."""
        self.run_recipe("build_with_move.lua", "local.build_with_move@v1")

        # Verify moves
        asset_path = self.get_asset_path("local.build_with_move@v1")
        self.assertIsNotNone(asset_path)

        # Source should not exist
        self.assertFalse((asset_path / "source_move.txt").exists())
        self.assertFalse((asset_path / "move_dir").exists())

        # Destination should exist
        self.assertTrue((asset_path / "moved_file.txt").exists())
        self.assertTrue((asset_path / "moved_dir").is_dir())
        self.assertTrue((asset_path / "moved_dir" / "content.txt").exists())

    def test_build_with_extract(self):
        """Build phase can extract archives with envy.extract()."""
        self.run_recipe("build_with_extract.lua", "local.build_with_extract@v1")

        # Verify extraction
        asset_path = self.get_asset_path("local.build_with_extract@v1")
        self.assertIsNotNone(asset_path)
        self.assertTrue((asset_path / "root").exists())
        self.assertTrue((asset_path / "root" / "file1.txt").exists())

    def test_build_multiple_operations(self):
        """Build phase can chain multiple operations."""
        self.run_recipe(
            "build_multiple_operations.lua", "local.build_multiple_operations@v1"
        )

        # Verify final state
        asset_path = self.get_asset_path("local.build_multiple_operations@v1")
        self.assertIsNotNone(asset_path)

        # Intermediate directories should be gone
        self.assertFalse((asset_path / "step2").exists())

        # Final directory should exist with all content
        self.assertTrue((asset_path / "final").is_dir())
        self.assertTrue((asset_path / "final" / "data.txt").exists())
        self.assertTrue((asset_path / "final" / "new.txt").exists())

        # Verify content has both modifications
        content = (asset_path / "final" / "data.txt").read_text()
        self.assertIn("step1_output", content)
        self.assertIn("step2_additional", content)

    def test_build_with_custom_env(self):
        """Build phase can set custom environment variables."""
        self.run_recipe("build_with_env.lua", "local.build_with_env@v1")
        # Success is verified by recipe not throwing an error

    def test_build_with_custom_cwd(self):
        """Build phase can run commands in custom working directory."""
        self.run_recipe("build_with_cwd.lua", "local.build_with_cwd@v1")

        # Verify files were created in correct locations
        asset_path = self.get_asset_path("local.build_with_cwd@v1")
        self.assertIsNotNone(asset_path)
        self.assertTrue((asset_path / "subdir" / "marker.txt").exists())
        self.assertTrue(
            (asset_path / "nested" / "deep" / "dir" / "deep_marker.txt").exists()
        )

    def test_build_error_nonzero_exit(self):
        """Build phase fails on non-zero exit code."""
        self.run_recipe(
            "build_error_nonzero_exit.lua",
            "local.build_error_nonzero_exit@v1",
            should_succeed=False,
        )
        # Recipe should fail with non-zero exit

    def test_build_string_error(self):
        """Build phase fails when shell script returns non-zero."""
        self.run_recipe(
            "build_string_error.lua",
            "local.build_string_error@v1",
            should_succeed=False,
        )
        # Recipe should fail with non-zero exit

    def test_build_access_directories(self):
        """Build phase has access to fetch_dir, stage_dir, install_dir."""
        self.run_recipe("build_access_dirs.lua", "local.build_access_dirs@v1")

        asset_path = self.get_asset_path("local.build_access_dirs@v1")
        self.assertIsNotNone(asset_path)
        # Recipe validates directory access internally

    def test_build_nested_directory_structure(self):
        """Build phase can create and manipulate nested directories."""
        self.run_recipe("build_nested_dirs.lua", "local.build_nested_dirs@v1")

        # Verify nested structure
        asset_path = self.get_asset_path("local.build_nested_dirs@v1")
        self.assertIsNotNone(asset_path)

        # Verify copied nested structure
        self.assertTrue((asset_path / "copied_output" / "bin" / "app").exists())
        self.assertTrue(
            (asset_path / "copied_output" / "lib" / "x86_64" / "libapp.so").exists()
        )
        self.assertTrue((asset_path / "copied_output" / "include" / "app.h").exists())
        self.assertTrue(
            (asset_path / "copied_output" / "include" / "subproject" / "sub.h").exists()
        )
        self.assertTrue(
            (asset_path / "copied_output" / "share" / "doc" / "README.md").exists()
        )

    def test_build_output_capture(self):
        """Build phase captures stdout correctly."""
        self.run_recipe("build_output_capture.lua", "local.build_output_capture@v1")
        # Success is verified by recipe validating captured output

    def test_build_function_returns_string(self):
        """Build function can return a string that gets executed."""
        self.run_recipe(
            "build_function_returns_string.lua", "local.build_function_returns_string@v1"
        )

        # Verify setup directory was created by function body
        asset_path = self.get_asset_path("local.build_function_returns_string@v1")
        self.assertIsNotNone(asset_path)
        self.assertTrue((asset_path / "setup_dir").exists())

        # Verify output from returned script was created
        self.assertTrue((asset_path / "output_from_returned_script").exists())
        self.assertTrue(
            (asset_path / "output_from_returned_script" / "marker.txt").exists()
        )

        content = (
            asset_path / "output_from_returned_script" / "marker.txt"
        ).read_text()
        self.assertEqual(content.strip(), "returned_script_artifact")

    def test_cache_path_includes_platform_arch(self):
        """Verify cache variant directory includes platform-arch prefix, not empty."""
        self.run_recipe("build_function.lua", "local.build_function@v1")

        # Find the variant subdirectory under the identity
        identity_dir = self.cache_root / "packages" / "local.build_function@v1"
        self.assertTrue(identity_dir.exists(), f"Identity dir should exist: {identity_dir}")

        variant_dirs = [d for d in identity_dir.iterdir() if d.is_dir()]
        self.assertEqual(len(variant_dirs), 1, "Should have exactly one variant directory")

        variant_name = variant_dirs[0].name
        # Verify format is {platform}-{arch}-blake3-{hash}, not --blake3-{hash}
        self.assertNotIn(
            variant_name.startswith("--blake3-"),
            [True],
            f"Variant dir should not start with '--blake3-' (missing platform/arch): {variant_name}",
        )

        # Verify it starts with a valid platform
        valid_platforms = ("darwin-", "linux-", "windows-")
        self.assertTrue(
            any(variant_name.startswith(p) for p in valid_platforms),
            f"Variant dir should start with platform prefix: {variant_name}",
        )

        # Verify it contains blake3 hash marker
        self.assertIn(
            "-blake3-",
            variant_name,
            f"Variant dir should contain '-blake3-': {variant_name}",
        )


if __name__ == "__main__":
    unittest.main()

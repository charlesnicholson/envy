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


class TestStagePhase(unittest.TestCase):
    """Tests for stage phase (archive extraction and preparation)."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-stage-test-"))
        self.envy_test = (
            Path(__file__).parent.parent / "out" / "build" / "envy_functional_tester"
        )
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)

    def get_asset_path(self, identity):
        """Find asset directory for given identity in cache.

        Args:
            identity: Recipe identity (e.g., "local.stage_default@v1")
        """
        assets_dir = self.cache_root / "assets" / identity
        if not assets_dir.exists():
            return None
        # Find the platform-specific asset subdirectory
        for subdir in assets_dir.iterdir():
            if subdir.is_dir():
                # Files are in the asset/ subdirectory
                asset_dir = subdir / "asset"
                if asset_dir.exists():
                    return asset_dir
                return subdir
        return None

    def test_default_stage_extracts_to_install_dir(self):
        """Recipe with no stage field auto-extracts archives."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.stage_default@v1",
                "test_data/recipes/stage_default.lua",
                f"--cache-root={self.cache_root}",
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
        """Recipe with stage = {strip=1} removes top-level directory."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.stage_declarative_strip@v1",
                "test_data/recipes/stage_declarative_strip.lua",
                f"--cache-root={self.cache_root}",
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
        """Recipe with stage function using ctx:extract_all works."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.stage_imperative@v1",
                "test_data/recipes/stage_imperative.lua",
                f"--cache-root={self.cache_root}",
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
        """Recipe with stage function using ctx:extract for single file works."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.stage_extract_single@v1",
                "test_data/recipes/stage_extract_single.lua",
                f"--cache-root={self.cache_root}",
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


if __name__ == "__main__":
    unittest.main()

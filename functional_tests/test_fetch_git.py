#!/usr/bin/env python3
"""Functional tests for git repository fetching.

Tests git repository support with ref specification (tags, branches, SHAs),
error handling, and basic integration scenarios.
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest

from . import test_config


class TestFetchGit(unittest.TestCase):
    """Tests for git repository fetching via fetch command."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-git-test-"))
        root = Path(__file__).resolve().parent.parent
        self.envy = test_config.get_envy_executable()
        self.envy_test = test_config.get_envy_executable()
        self.project_root = root
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)

    # ========================================================================
    # Basic Success Cases
    # ========================================================================

    def test_clone_public_https_tag(self):
        """Clone a public HTTPS repository with a valid tag."""
        with tempfile.TemporaryDirectory() as temp_dir:
            dest = Path(temp_dir) / "ninja"

            result = subprocess.run(
                [
                    str(self.envy),
                    "fetch",
                    "https://github.com/ninja-build/ninja.git",
                    str(dest),
                    "--ref",
                    "v1.11.1",
                ],
                capture_output=True,
                text=True,
                env={**os.environ, "ENVY_CACHE_DIR": str(self.cache_root)},
            )

            self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
            self.assertTrue(dest.exists(), f"Destination {dest} should exist")
            self.assertTrue((dest / "README.md").exists(), "README.md should exist")
            self.assertTrue((dest / "src").is_dir(), "src directory should exist")

            # Verify .git directory is present (kept for packages that need it)
            self.assertTrue(
                (dest / ".git").exists(), ".git directory should be present"
            )

    def test_clone_public_https_branch(self):
        """Clone a public HTTPS repository with a branch ref."""
        with tempfile.TemporaryDirectory() as temp_dir:
            dest = Path(temp_dir) / "ninja"

            result = subprocess.run(
                [
                    str(self.envy),
                    "fetch",
                    "https://github.com/ninja-build/ninja.git",
                    str(dest),
                    "--ref",
                    "master",  # ninja's default branch
                ],
                capture_output=True,
                text=True,
                env={**os.environ, "ENVY_CACHE_DIR": str(self.cache_root)},
            )

            self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
            self.assertTrue(dest.exists())
            self.assertTrue(
                (dest / ".git").exists(), ".git directory should be present"
            )

    def test_verify_file_contents_correct(self):
        """Verify that cloned repository has correct file contents."""
        with tempfile.TemporaryDirectory() as temp_dir:
            dest = Path(temp_dir) / "ninja"

            result = subprocess.run(
                [
                    str(self.envy),
                    "fetch",
                    "https://github.com/ninja-build/ninja.git",
                    str(dest),
                    "--ref",
                    "v1.11.1",
                ],
                capture_output=True,
                text=True,
                env={**os.environ, "ENVY_CACHE_DIR": str(self.cache_root)},
            )

            self.assertEqual(result.returncode, 0)

            # Check specific file content
            readme = dest / "README.md"
            self.assertTrue(readme.exists())
            content = readme.read_text()
            self.assertIn("Ninja", content)

            # Check directory structure
            self.assertTrue((dest / "src" / "ninja.cc").exists())
            self.assertTrue((dest / "configure.py").exists())

    # ========================================================================
    # Error Cases
    # ========================================================================

    def test_nonexistent_repository(self):
        """Non-existent repository URL should fail."""
        with tempfile.TemporaryDirectory() as temp_dir:
            dest = Path(temp_dir) / "output"

            result = subprocess.run(
                [
                    str(self.envy),
                    "fetch",
                    "https://github.com/this-org-does-not-exist-12345/repo.git",
                    str(dest),
                    "--ref",
                    "main",
                ],
                capture_output=True,
                text=True,
                env={**os.environ, "ENVY_CACHE_DIR": str(self.cache_root)},
            )

            self.assertNotEqual(result.returncode, 0)
            stderr_lower = result.stderr.lower()
            self.assertTrue(
                "clone failed" in stderr_lower or "failed" in stderr_lower,
                f"Expected clone error, got: {result.stderr}",
            )

    def test_valid_repo_nonexistent_ref(self):
        """Valid repository but non-existent ref should fail."""
        with tempfile.TemporaryDirectory() as temp_dir:
            dest = Path(temp_dir) / "ninja"

            result = subprocess.run(
                [
                    str(self.envy),
                    "fetch",
                    "https://github.com/ninja-build/ninja.git",
                    str(dest),
                    "--ref",
                    "this-ref-does-not-exist-12345",
                ],
                capture_output=True,
                text=True,
                env={**os.environ, "ENVY_CACHE_DIR": str(self.cache_root)},
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertGreater(len(result.stderr), 0)

    def test_missing_ref_flag(self):
        """Git URL without --ref flag should fail."""
        with tempfile.TemporaryDirectory() as temp_dir:
            dest = Path(temp_dir) / "output"

            result = subprocess.run(
                [
                    str(self.envy),
                    "fetch",
                    "https://github.com/ninja-build/ninja.git",
                    str(dest),
                ],
                capture_output=True,
                text=True,
                env={**os.environ, "ENVY_CACHE_DIR": str(self.cache_root)},
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("ref", result.stderr.lower())

    # ========================================================================
    # Edge Cases
    # ========================================================================

    def test_clone_to_path_with_spaces(self):
        """Clone to a destination path containing spaces."""
        with tempfile.TemporaryDirectory() as temp_dir:
            dest = Path(temp_dir) / "path with spaces" / "ninja build"

            result = subprocess.run(
                [
                    str(self.envy),
                    "fetch",
                    "https://github.com/ninja-build/ninja.git",
                    str(dest),
                    "--ref",
                    "v1.11.1",
                ],
                capture_output=True,
                text=True,
                env={**os.environ, "ENVY_CACHE_DIR": str(self.cache_root)},
            )

            self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
            self.assertTrue(dest.exists())
            self.assertTrue((dest / "README.md").exists())

    # ========================================================================
    # Integration with Recipe System
    # ========================================================================

    def test_recipe_with_git_source(self):
        """Recipe manifest with git source + ref loads correctly."""
        recipe_content = """-- Test recipe with git source
IDENTITY = "test.ninja@v1"

FETCH = { source = "https://github.com/ninja-build/ninja.git", ref = "v1.11.1" }

function CHECK(project_root, options)
    return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Nothing needed - source is already fetched by git
end
"""
        recipe_path = self.cache_root / "ninja_recipe.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "test.ninja@v1",
                str(recipe_path),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("test.ninja@v1", result.stdout)

    # ========================================================================
    # Parallel Git Fetching
    # ========================================================================

    def test_parallel_git_fetch(self):
        """Recipe with multiple git sources fetches concurrently (programmatic)."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.fetch_git_parallel@v1",
                "test_data/recipes/fetch_git_parallel.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.fetch_git_parallel@v1", result.stdout)

    def test_parallel_git_fetch_declarative(self):
        """Recipe with multiple git sources fetches concurrently (declarative)."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.fetch_git_parallel_declarative@v1",
                "test_data/recipes/fetch_git_parallel_declarative.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.fetch_git_parallel_declarative@v1", result.stdout)


if __name__ == "__main__":
    unittest.main()

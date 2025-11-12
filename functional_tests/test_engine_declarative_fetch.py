#!/usr/bin/env python3
"""Functional tests for engine declarative fetch.

Tests declarative fetch syntax: fetch = "url", fetch = {url, sha256},
fetch = [{...}, ...], and basic error handling (collision, bad SHA256).
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest


class TestEngineDeclarativeFetch(unittest.TestCase):
    """Tests for declarative fetch phase (asset fetching)."""

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
  source = "test_data/lua/simple.lua",
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
    source = "test_data/lua/simple.lua",
    sha256 = "{simple_hash}"
  }},
  {{
    source = "test_data/lua/print_single.lua",
    sha256 = "{print_single_hash}"
  }},
  {{
    source = "test_data/lua/print_multiple.lua"
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

    def test_declarative_fetch_string_array(self):
        """Recipe with fetch = {\"url1\", \"url2\", \"url3\"} downloads all files."""
        # Create recipe with array of strings (no SHA256)
        recipe_content = """-- Test declarative fetch with string array
identity = "local.fetch_string_array@v1"

-- Array of strings (no SHA256 verification)
fetch = {
  "test_data/lua/simple.lua",
  "test_data/lua/print_single.lua",
  "test_data/lua/print_multiple.lua"
}
"""
        recipe_path = self.cache_root / "fetch_string_array.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                "--trace",
                "engine-test",
                "local.fetch_string_array@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 1)
        self.assertIn("local.fetch_string_array@v1", result.stdout)

        # Verify downloading log mentions 3 files
        stderr_lower = result.stderr.lower()
        self.assertIn("downloading", stderr_lower, f"Expected download log: {result.stderr}")
        self.assertIn("3", result.stderr, f"Expected 3 files mentioned: {result.stderr}")

    def test_declarative_fetch_git(self):
        """Recipe with git fetch downloads repository."""
        recipe_content = """-- Test declarative fetch with git repository
identity = "local.fetch_git_test@v1"

fetch = {
    source = "https://github.com/ninja-build/ninja.git",
    ref = "v1.11.1"
}

function check(ctx)
    return false
end

function install(ctx)
    -- Verify the fetched git repo is available in stage_dir/ninja.git/
    ctx.ls(ctx.stage_dir)
    local readme = ctx.stage_dir .. "/ninja.git/README.md"
    local f = io.open(readme, "r")
    if not f then
        error("Could not find README.md at: " .. readme)
    end
    f:close()

    -- Verify .git was removed
    local git_dir = ctx.stage_dir .. "/ninja.git/.git"
    local g = io.open(git_dir, "r")
    if g then
        g:close()
        error(".git directory should have been removed")
    end
end
"""
        recipe_path = self.cache_root / "fetch_git_test.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.fetch_git_test@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.fetch_git_test@v1", result.stdout)

    def test_declarative_git_in_stage_not_fetch(self):
        """Git repos must be cloned to stage_dir, NOT fetch_dir."""
        recipe_content = """-- Test that git repos go to stage_dir
identity = "local.git_location_test@v1"

fetch = {
    source = "https://github.com/ninja-build/ninja.git",
    ref = "v1.11.1"
}

function check(ctx)
    return false
end

function install(ctx)
    -- Verify git repo is in stage_dir
    local stage_readme = ctx.stage_dir .. "/ninja.git/README.md"
    local f = io.open(stage_readme, "r")
    if not f then
        error("Git repo not found in stage_dir at: " .. stage_readme)
    end
    f:close()

    -- Verify git repo is NOT in fetch_dir
    local fetch_readme = ctx.fetch_dir .. "/ninja.git/README.md"
    local g = io.open(fetch_readme, "r")
    if g then
        g:close()
        error("Git repo should NOT be in fetch_dir, found at: " .. fetch_readme)
    end

    ctx.mark_install_complete()
end
"""
        recipe_path = self.cache_root / "git_location_test.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.git_location_test@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_declarative_git_no_fetch_complete_marker(self):
        """Git fetches should NOT create fetch completion marker (not cacheable)."""
        recipe_content = """-- Test git fetch completion marker
identity = "local.git_no_cache@v1"

fetch = {
    source = "https://github.com/ninja-build/ninja.git",
    ref = "v1.11.1"
}

function check(ctx)
    return false
end

function install(ctx)
    local readme = ctx.stage_dir .. "/ninja.git/README.md"
    local f = io.open(readme, "r")
    if not f then
        error("Git repo not found")
    end
    f:close()
    ctx.mark_install_complete()
end
"""
        recipe_path = self.cache_root / "git_no_cache.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                "--trace",
                "engine-test",
                "local.git_no_cache@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Verify trace log shows skipping fetch completion
        self.assertIn("skipping fetch completion marker", result.stderr.lower())

        # Verify fetch completion marker does NOT exist
        asset_dir = self.cache_root / "assets" / "local.git_no_cache@v1"
        fetch_complete_files = list(asset_dir.rglob("fetch/envy-complete"))
        self.assertEqual(len(fetch_complete_files), 0,
                        f"fetch/envy-complete should not exist for git repos, found: {fetch_complete_files}")



if __name__ == "__main__":
    unittest.main()

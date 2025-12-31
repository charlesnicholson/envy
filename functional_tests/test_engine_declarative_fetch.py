#!/usr/bin/env python3
"""Functional tests for engine declarative fetch.

Tests declarative fetch syntax: FETCH = "url", FETCH = {url, sha256},
FETCH = [{...}, ...], and basic error handling (collision, bad SHA256).
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest

from . import test_config


class TestEngineDeclarativeFetch(unittest.TestCase):
    """Tests for declarative fetch phase (asset fetching)."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-engine-test-"))
        self.envy_test = test_config.get_envy_executable()
        self.envy = test_config.get_envy_executable()
        # Enable trace for all tests if ENVY_TEST_TRACE is set
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)

    @staticmethod
    def lua_path(path: Path) -> str:
        """Convert path to Lua-safe string (forward slashes work on all platforms)."""
        return path.as_posix()

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
        """Spec with declarative fetch (string format) downloads file."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                "--trace",
                "engine-test",
                "local.fetch_string@v1",
                "test_data/specs/fetch_string.lua",
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
        """Spec with declarative fetch (single table with sha256) downloads and verifies."""
        # Compute hash dynamically
        simple_hash = self.get_file_hash("test_data/lua/simple.lua")

        # Create spec with computed hash
        spec_content = f"""-- Test declarative fetch with single table format and SHA256 verification
IDENTITY = "local.fetch_single@v1"

-- Single table format with optional sha256
FETCH = {{
  source = "test_data/lua/simple.lua",
  sha256 = "{simple_hash}"
}}
"""
        modified_spec = self.cache_root / "fetch_single.lua"
        modified_spec.write_text(spec_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                "--trace",
                "engine-test",
                "local.fetch_single@v1",
                str(modified_spec),
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
        """Spec with declarative fetch (array format) downloads multiple files concurrently."""
        # Compute hashes dynamically
        simple_hash = self.get_file_hash("test_data/lua/simple.lua")
        print_single_hash = self.get_file_hash("test_data/lua/print_single.lua")

        # Create spec with computed hashes
        spec_content = f"""-- Test declarative fetch with array format (concurrent downloads)
IDENTITY = "local.fetch_array@v1"

-- Array format: multiple files with optional sha256
FETCH = {{
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
        modified_spec = self.cache_root / "fetch_array.lua"
        modified_spec.write_text(spec_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                "--trace",
                "engine-test",
                "local.fetch_array@v1",
                str(modified_spec),
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
        """Spec with duplicate filenames fails with collision error."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.fetch_collision@v1",
                "test_data/specs/fetch_collision.lua",
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
        """Spec with wrong SHA256 fails verification."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.fetch_bad_sha256@v1",
                "test_data/specs/fetch_bad_sha256.lua",
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
        """Spec with FETCH = {\"url1\", \"url2\", \"url3\"} downloads all files."""
        # Create spec with array of strings (no SHA256)
        spec_content = """-- Test declarative fetch with string array
IDENTITY = "local.fetch_string_array@v1"

-- Array of strings (no SHA256 verification)
FETCH = {
  "test_data/lua/simple.lua",
  "test_data/lua/print_single.lua",
  "test_data/lua/print_multiple.lua"
}
"""
        spec_path = self.cache_root / "fetch_string_array.lua"
        spec_path.write_text(spec_content, encoding="utf-8")

        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                "--trace",
                "engine-test",
                "local.fetch_string_array@v1",
                str(spec_path),
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
        self.assertIn(
            "downloading", stderr_lower, f"Expected download log: {result.stderr}"
        )
        self.assertIn(
            "3", result.stderr, f"Expected 3 files mentioned: {result.stderr}"
        )

    def test_declarative_fetch_git(self):
        """Spec with git fetch downloads repository."""
        spec_content = """-- Test declarative fetch with git repository
IDENTITY = "local.fetch_git_test@v1"

FETCH = {
    source = "https://github.com/ninja-build/ninja.git",
    ref = "v1.13.2"
}

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Verify the fetched git repo is available in stage_dir/ninja.git/
    local readme = stage_dir .. "/ninja.git/README.md"
    local f = io.open(readme, "r")
    if not f then
        error("Could not find README.md at: " .. readme)
    end
    f:close()

    -- Verify .git directory is present (kept for packages that need it)
    -- Check by opening a file that must exist in a git repo
    local git_head = stage_dir .. "/ninja.git/.git/HEAD"
    local g = io.open(git_head, "r")
    if not g then
        error(".git directory should be present at: " .. stage_dir .. "/ninja.git/.git (tried to open HEAD file)")
    end
    g:close()
end
"""
        spec_path = self.cache_root / "fetch_git_test.lua"
        spec_path.write_text(spec_content, encoding="utf-8")

        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.fetch_git_test@v1",
                str(spec_path),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.fetch_git_test@v1", result.stdout)

    def test_declarative_git_in_stage_not_fetch(self):
        """Git repos must be cloned to stage_dir, NOT fetch_dir."""
        # This is a cache-managed package (no check verb) that verifies git placement
        spec_content = """-- Test that git repos go to stage_dir
IDENTITY = "local.git_location_test@v1"

FETCH = {
    source = "https://github.com/ninja-build/ninja.git",
    ref = "v1.13.2"
}

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Verify git repo is in stage_dir
    local stage_readme = stage_dir .. "/ninja.git/README.md"
    local f = io.open(stage_readme, "r")
    if not f then
        error("Git repo not found in stage_dir at: " .. stage_readme)
    end
    f:close()

    -- Verify git repo is NOT in fetch_dir
    local fetch_readme = fetch_dir .. "/ninja.git/README.md"
    local g = io.open(fetch_readme, "r")
    if g then
        g:close()
        error("Git repo should NOT be in fetch_dir, found at: " .. fetch_readme)
    end

end
"""
        spec_path = self.cache_root / "git_location_test.lua"
        spec_path.write_text(spec_content, encoding="utf-8")

        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.git_location_test@v1",
                str(spec_path),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_declarative_git_no_fetch_complete_marker(self):
        """Git fetches should NOT create fetch completion marker (not cacheable)."""
        # This is a cache-managed package (no check verb) that verifies fetch marker behavior
        spec_content = """-- Test git fetch completion marker
IDENTITY = "local.git_no_cache@v1"

FETCH = {
    source = "https://github.com/ninja-build/ninja.git",
    ref = "v1.13.2"
}

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    local readme = stage_dir .. "/ninja.git/README.md"
    local f = io.open(readme, "r")
    if not f then
        error("Git repo not found")
    end
    f:close()
end
"""
        spec_path = self.cache_root / "git_no_cache.lua"
        spec_path.write_text(spec_content, encoding="utf-8")

        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                "--trace",
                "engine-test",
                "local.git_no_cache@v1",
                str(spec_path),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Verify trace log shows skipping fetch completion
        self.assertIn("skipping fetch completion marker", result.stderr.lower())

        # Verify fetch completion marker does NOT exist
        asset_dir = self.cache_root / "packages" / "local.git_no_cache@v1"
        fetch_complete_files = list(asset_dir.rglob("fetch/envy-complete"))
        self.assertEqual(
            len(fetch_complete_files),
            0,
            f"fetch/envy-complete should not exist for git repos, found: {fetch_complete_files}",
        )


if __name__ == "__main__":
    unittest.main()

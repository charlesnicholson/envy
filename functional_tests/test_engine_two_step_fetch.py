#!/usr/bin/env python3
"""Functional tests for two-step fetch pattern (fetch → commit).

Tests the security gating pattern where ctx.fetch() downloads to tmp
and ctx.commit_fetch() moves files to fetch_dir with SHA256 verification.
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest

from . import test_config


class TestEngineTwoStepFetch(unittest.TestCase):
    """Tests for two-step fetch pattern (ungated fetch → gated commit)."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-two-step-fetch-"))
        self.envy_test = test_config.get_envy_executable()
        self.envy = test_config.get_envy_executable()
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

    def test_two_step_with_sha256(self):
        """Fetch → inspect → commit with SHA256 verification."""
        # Get actual SHA256 of test file
        test_file = Path("test_data/lua/simple.lua").resolve()
        expected_hash = self.get_file_hash(test_file)

        recipe_content = f"""identity = "local.two_step_sha256@v1"

function fetch(ctx)
  -- Step 1: Download to tmp (ungated)
  local file = ctx.fetch("{self.lua_path(test_file)}")

  -- At this point, file is in ctx.tmp (run_dir), not fetch_dir
  -- User could inspect it, read manifest, fetch more files, etc.

  -- Step 2: Commit with SHA256 (gated)
  ctx.commit_fetch({{
    filename = file,
    sha256 = "{expected_hash}"
  }})

  -- Now file is in fetch_dir with verified SHA256
end
"""
        recipe_path = self.cache_root / "two_step_sha256.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.two_step_sha256@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.two_step_sha256@v1", result.stdout)

    def test_fetch_then_inspect_then_commit(self):
        """Fetch manifest → read contents → fetch listed files → commit all."""
        # Create a fake manifest listing other files
        manifest_dir = self.cache_root / "manifest_files"
        manifest_dir.mkdir()

        # Create manifest file
        manifest_file = manifest_dir / "manifest.txt"
        manifest_file.write_text("file1.txt\nfile2.txt\n", encoding="utf-8")

        # Create listed files
        (manifest_dir / "file1.txt").write_text("content1", encoding="utf-8")
        (manifest_dir / "file2.txt").write_text("content2", encoding="utf-8")

        recipe_content = f"""identity = "local.manifest_workflow@v1"

function fetch(ctx)
  -- Step 1: Fetch manifest
  local manifest_file = ctx.fetch("{self.lua_path(manifest_file)}")

  -- Step 2: Read manifest from tmp (this is the point of two-step pattern!)
  local manifest_path = ctx.tmp .. "/" .. manifest_file
  local f = io.open(manifest_path, "r")
  if not f then error("Cannot read manifest from tmp") end
  local manifest_content = f:read("*all")
  f:close()

  -- Verify we can inspect files before commit
  if not manifest_content:match("file1.txt") then
    error("Manifest should list file1.txt")
  end

  -- Step 3: Fetch files listed in manifest
  local files = ctx.fetch({{
    "{self.lua_path(manifest_dir / 'file1.txt')}",
    "{self.lua_path(manifest_dir / 'file2.txt')}"
  }})

  -- Step 4: Commit all files (manifest + listed files)
  ctx.commit_fetch(manifest_file)
  ctx.commit_fetch(files)
end
"""
        recipe_path = self.cache_root / "manifest_workflow.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.manifest_workflow@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.manifest_workflow@v1", result.stdout)


if __name__ == "__main__":
    unittest.main()

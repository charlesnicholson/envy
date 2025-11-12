#!/usr/bin/env python3
"""Functional tests for engine programmatic fetch (fetch functions).

Tests fetch = function(ctx) ... end syntax with ctx.fetch() and ctx.commit_fetch().
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest


class TestEngineProgrammaticFetch(unittest.TestCase):
    """Tests for programmatic fetch phase (fetch functions)."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-prog-fetch-test-"))
        self.envy_test = (
            Path(__file__).parent.parent / "out" / "build" / "envy_functional_tester"
        )
        self.envy = Path(__file__).parent.parent / "out" / "build" / "envy"
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

    def test_fetch_single_string(self):
        """ctx.fetch(\"url\") returns scalar string basename."""
        recipe_content = """identity = "local.prog_fetch_single@v1"

function fetch(ctx)
  local file = ctx.fetch("test_data/lua/simple.lua")

  -- Verify return is scalar string, not array
  if type(file) ~= "string" then
    error("Expected string return, got " .. type(file))
  end

  ctx.commit_fetch(file)
end
"""
        recipe_path = self.cache_root / "prog_fetch_single.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_fetch_single@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.prog_fetch_single@v1", result.stdout)

    def test_fetch_string_array(self):
        """ctx.fetch({\"url1\", \"url2\"}) returns array of basenames."""
        recipe_content = """identity = "local.prog_fetch_array@v1"

function fetch(ctx)
  local files = ctx.fetch({
    "test_data/lua/simple.lua",
    "test_data/lua/print_single.lua"
  })

  -- Verify return is array
  if type(files) ~= "table" then
    error("Expected table return, got " .. type(files))
  end
  if #files ~= 2 then
    error("Expected 2 files, got " .. #files)
  end

  ctx.commit_fetch(files)
end
"""
        recipe_path = self.cache_root / "prog_fetch_array.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_fetch_array@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_fetch_single_table(self):
        """ctx.fetch({url=\"...\"}) returns scalar basename."""
        recipe_content = """identity = "local.prog_fetch_table@v1"

function fetch(ctx)
  local file = ctx.fetch({source = "test_data/lua/simple.lua"})

  -- Verify return is scalar string
  if type(file) ~= "string" then
    error("Expected string return, got " .. type(file))
  end

  ctx.commit_fetch(file)
end
"""
        recipe_path = self.cache_root / "prog_fetch_table.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_fetch_table@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_fetch_table_array(self):
        """ctx.fetch({{url=\"...\"}, {...}}) returns array."""
        recipe_content = """identity = "local.prog_fetch_table_array@v1"

function fetch(ctx)
  local files = ctx.fetch({
    {source = "test_data/lua/simple.lua"},
    {source = "test_data/lua/print_single.lua"}
  })

  -- Verify return is array
  if type(files) ~= "table" or #files ~= 2 then
    error("Expected array of 2 files")
  end

  ctx.commit_fetch(files)
end
"""
        recipe_path = self.cache_root / "prog_fetch_table_array.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_fetch_table_array@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_commit_fetch_scalar_string(self):
        """ctx.commit_fetch(\"file.tar.gz\") moves file to fetch_dir."""
        recipe_content = """identity = "local.prog_commit_scalar@v1"

function fetch(ctx)
  local file = ctx.fetch("test_data/lua/simple.lua")

  -- File should be in tmp before commit
  local tmp_path = ctx.tmp .. "/" .. file
  local f = io.open(tmp_path, "r")
  if not f then
    error("File not in tmp: " .. tmp_path)
  end
  f:close()

  ctx.commit_fetch(file)
end
"""
        recipe_path = self.cache_root / "prog_commit_scalar.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_commit_scalar@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_commit_fetch_with_sha256(self):
        """ctx.commit_fetch({filename=\"...\", sha256=\"...\"}) verifies hash."""
        # Compute hash dynamically
        test_file = Path("test_data/lua/simple.lua")
        file_hash = self.get_file_hash(test_file)

        recipe_content = f"""identity = "local.prog_commit_sha256@v1"

function fetch(ctx)
  local file = ctx.fetch("test_data/lua/simple.lua")

  -- Commit with SHA256 verification
  ctx.commit_fetch({{
    filename = file,
    sha256 = "{file_hash}"
  }})
end
"""
        recipe_path = self.cache_root / "prog_commit_sha256.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                "--trace",
                "engine-test",
                "local.prog_commit_sha256@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        # Verify SHA256 verification happened (requires --trace to see the log)
        self.assertIn("sha256", result.stderr.lower())

    def test_commit_fetch_sha256_mismatch(self):
        """Wrong SHA256 in commit_fetch causes verification error."""
        recipe_content = """identity = "local.prog_commit_bad_sha256@v1"

function fetch(ctx)
  local file = ctx.fetch("test_data/lua/simple.lua")

  -- Commit with wrong SHA256
  ctx.commit_fetch({
    filename = file,
    sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
  })
end
"""
        recipe_path = self.cache_root / "prog_commit_bad_sha256.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_commit_bad_sha256@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(result.returncode, 0, "Expected SHA256 mismatch to fail")
        self.assertIn("sha256", result.stderr.lower())

    def test_commit_fetch_array(self):
        """ctx.commit_fetch({\"file1\", \"file2\"}) commits multiple files."""
        recipe_content = """identity = "local.prog_commit_array@v1"

function fetch(ctx)
  local files = ctx.fetch({
    "test_data/lua/simple.lua",
    "test_data/lua/print_single.lua"
  })

  ctx.commit_fetch(files)
end
"""
        recipe_path = self.cache_root / "prog_commit_array.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_commit_array@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_commit_fetch_missing_file(self):
        """Trying to commit file not in ctx.tmp fails with clear error."""
        recipe_content = """identity = "local.prog_commit_missing@v1"

function fetch(ctx)
  -- Try to commit file that was never fetched
  ctx.commit_fetch("nonexistent.tar.gz")
end
"""
        recipe_path = self.cache_root / "prog_commit_missing.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_commit_missing@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(result.returncode, 0, "Expected missing file error")
        stderr_lower = result.stderr.lower()
        self.assertTrue(
            "not found" in stderr_lower or "missing" in stderr_lower,
            f"Expected file not found error: {result.stderr}",
        )

    def test_selective_commit(self):
        """Fetch 3 files, commit 2, verify tmp cleanup removes uncommitted."""
        recipe_content = """identity = "local.prog_selective_commit@v1"

function fetch(ctx)
  local files = ctx.fetch({
    "test_data/lua/simple.lua",
    "test_data/lua/print_single.lua",
    "test_data/lua/print_multiple.lua"
  })

  -- Only commit first 2
  ctx.commit_fetch({files[1], files[2]})

  -- Third file should still be in tmp here but will be cleaned up
end
"""
        recipe_path = self.cache_root / "prog_selective_commit.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_selective_commit@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_ctx_identity(self):
        """ctx.identity contains recipe identity."""
        recipe_content = """identity = "local.prog_ctx_identity@v1"

function fetch(ctx)
  if ctx.identity ~= "local.prog_ctx_identity@v1" then
    error("Expected identity 'local.prog_ctx_identity@v1', got: " .. ctx.identity)
  end

  -- Fetch something so phase completes successfully
  local file = ctx.fetch("test_data/lua/simple.lua")
  ctx.commit_fetch(file)
end
"""
        recipe_path = self.cache_root / "prog_ctx_identity.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_ctx_identity@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_ctx_options(self):
        """ctx.options is accessible as a table (empty when no options passed)."""
        recipe_content = """identity = "local.prog_ctx_options@v1"

function fetch(ctx)
  -- Verify ctx.options exists and is a table
  if type(ctx.options) ~= "table" then
    error("Expected ctx.options to be table, got: " .. type(ctx.options))
  end

  local file = ctx.fetch("test_data/lua/simple.lua")
  ctx.commit_fetch(file)
end
"""
        recipe_path = self.cache_root / "prog_ctx_options.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_ctx_options@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_ctx_options_empty(self):
        """ctx.options exists as empty table when no options specified."""
        recipe_content = """identity = "local.prog_ctx_options_empty@v1"

function fetch(ctx)
  if type(ctx.options) ~= "table" then
    error("Expected ctx.options to be table, got: " .. type(ctx.options))
  end

  -- Check it's empty
  local count = 0
  for k, v in pairs(ctx.options) do
    count = count + 1
  end

  if count ~= 0 then
    error("Expected empty options, got " .. count .. " entries")
  end

  local file = ctx.fetch("test_data/lua/simple.lua")
  ctx.commit_fetch(file)
end
"""
        recipe_path = self.cache_root / "prog_ctx_options_empty.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_ctx_options_empty@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_multiple_serial_fetches(self):
        """Multiple ctx.fetch() calls execute serially, files accumulate."""
        recipe_content = """identity = "local.prog_serial_fetches@v1"

function fetch(ctx)
  -- First fetch
  local file1 = ctx.fetch("test_data/lua/simple.lua")

  -- Verify file1 exists in tmp
  local f = io.open(ctx.tmp .. "/" .. file1, "r")
  if not f then error("file1 not in tmp after first fetch") end
  f:close()

  -- Second fetch
  local file2 = ctx.fetch("test_data/lua/print_single.lua")

  -- Verify both files exist
  f = io.open(ctx.tmp .. "/" .. file1, "r")
  if not f then error("file1 disappeared after second fetch") end
  f:close()

  f = io.open(ctx.tmp .. "/" .. file2, "r")
  if not f then error("file2 not in tmp after second fetch") end
  f:close()

  -- Third fetch
  local file3 = ctx.fetch("test_data/lua/print_multiple.lua")

  -- Commit all
  ctx.commit_fetch({file1, file2, file3})
end
"""
        recipe_path = self.cache_root / "prog_serial_fetches.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_serial_fetches@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_fetch_function_error_propagation(self):
        """Lua errors in fetch function propagate with recipe identity."""
        recipe_content = """identity = "local.prog_error_prop@v1"

function fetch(ctx)
  error("Intentional test error")
end
"""
        recipe_path = self.cache_root / "prog_error_prop.lua"
        recipe_path.write_text(recipe_content)

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_error_prop@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(result.returncode, 0, "Expected error to cause failure")
        # Verify error mentions recipe identity
        self.assertIn("local.prog_error_prop@v1", result.stderr)
        self.assertIn("Intentional test error", result.stderr)


if __name__ == "__main__":
    unittest.main()

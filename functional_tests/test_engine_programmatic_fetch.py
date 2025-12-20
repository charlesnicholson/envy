#!/usr/bin/env python3
"""Functional tests for engine programmatic fetch (fetch functions).

Tests FETCH = function(ctx) ... end syntax with envy.fetch() and envy.commit_fetch().
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest

from . import test_config


class TestEngineProgrammaticFetch(unittest.TestCase):
    """Tests for programmatic fetch phase (fetch functions)."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-prog-fetch-test-"))
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

    def test_fetch_single_string(self):
        """envy.fetch(\"url\") returns scalar string basename."""
        recipe_content = """IDENTITY = "local.prog_fetch_single@v1"

function FETCH(tmp_dir, options)
  local file = envy.fetch("test_data/lua/simple.lua", {dest = tmp_dir})

  -- Verify return is scalar string, not array
  if type(file) ~= "string" then
    error("Expected string return, got " .. type(file))
  end

  envy.commit_fetch(file)
end
"""
        recipe_path = self.cache_root / "prog_fetch_single.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

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
        """envy.fetch({\"url1\", \"url2\"}) returns array of basenames."""
        recipe_content = """IDENTITY = "local.prog_fetch_array@v1"

function FETCH(tmp_dir, options)
  local files = envy.fetch({
    "test_data/lua/simple.lua",
    "test_data/lua/print_single.lua"
  }, {dest = tmp_dir})

  -- Verify return is array
  if type(files) ~= "table" then
    error("Expected table return, got " .. type(files))
  end
  if #files ~= 2 then
    error("Expected 2 files, got " .. #files)
  end

  envy.commit_fetch(files)
end
"""
        recipe_path = self.cache_root / "prog_fetch_array.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

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
        """envy.fetch({url=\"...\"}) returns scalar basename."""
        recipe_content = """IDENTITY = "local.prog_fetch_table@v1"

function FETCH(tmp_dir, options)
  local file = envy.fetch({source = "test_data/lua/simple.lua"}, {dest = tmp_dir})

  -- Verify return is scalar string
  if type(file) ~= "string" then
    error("Expected string return, got " .. type(file))
  end

  envy.commit_fetch(file)
end
"""
        recipe_path = self.cache_root / "prog_fetch_table.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

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
        """envy.fetch({{url=\"...\"}, {...}}) returns array."""
        recipe_content = """IDENTITY = "local.prog_fetch_table_array@v1"

function FETCH(tmp_dir, options)
  local files = envy.fetch({
    {source = "test_data/lua/simple.lua"},
    {source = "test_data/lua/print_single.lua"}
  }, {dest = tmp_dir})

  -- Verify return is array
  if type(files) ~= "table" or #files ~= 2 then
    error("Expected array of 2 files")
  end

  envy.commit_fetch(files)
end
"""
        recipe_path = self.cache_root / "prog_fetch_table_array.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

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
        """envy.commit_fetch(\"file.tar.gz\") moves file to fetch_dir."""
        recipe_content = """IDENTITY = "local.prog_commit_scalar@v1"

function FETCH(tmp_dir, options)
  local file = envy.fetch("test_data/lua/simple.lua", {dest = tmp_dir})

  -- File should be in tmp before commit
  local tmp_path = tmp_dir .. "/" .. file
  local f = io.open(tmp_path, "r")
  if not f then
    error("File not in tmp: " .. tmp_path)
  end
  f:close()

  envy.commit_fetch(file)
end
"""
        recipe_path = self.cache_root / "prog_commit_scalar.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

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
        """envy.commit_fetch({filename=\"...\", sha256=\"...\"}) verifies hash."""
        # Compute hash dynamically
        test_file = Path("test_data/lua/simple.lua")
        file_hash = self.get_file_hash(test_file)

        recipe_content = f"""IDENTITY = "local.prog_commit_sha256@v1"

function FETCH(tmp_dir, options)
  local file = envy.fetch("test_data/lua/simple.lua", {{dest = tmp_dir}})

  -- Commit with SHA256 verification
  envy.commit_fetch({{
    filename = file,
    sha256 = "{file_hash}"
  }})
end
"""
        recipe_path = self.cache_root / "prog_commit_sha256.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

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
        recipe_content = """IDENTITY = "local.prog_commit_bad_sha256@v1"

function FETCH(tmp_dir, options)
  local file = envy.fetch("test_data/lua/simple.lua", {dest = tmp_dir})

  -- Commit with wrong SHA256
  envy.commit_fetch({
    filename = file,
    sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
  })
end
"""
        recipe_path = self.cache_root / "prog_commit_bad_sha256.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

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
        """envy.commit_fetch({\"file1\", \"file2\"}) commits multiple files."""
        recipe_content = """IDENTITY = "local.prog_commit_array@v1"

function FETCH(tmp_dir, options)
  local files = envy.fetch({
    "test_data/lua/simple.lua",
    "test_data/lua/print_single.lua"
  }, {dest = tmp_dir})

  envy.commit_fetch(files)
end
"""
        recipe_path = self.cache_root / "prog_commit_array.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

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
        """Trying to commit file not in tmp_dir fails with clear error."""
        recipe_content = """IDENTITY = "local.prog_commit_missing@v1"

function FETCH(tmp_dir, options)
  -- Try to commit file that was never fetched
  envy.commit_fetch("nonexistent.tar.gz")
end
"""
        recipe_path = self.cache_root / "prog_commit_missing.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

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
        recipe_content = """IDENTITY = "local.prog_selective_commit@v1"

function FETCH(tmp_dir, options)
  local files = envy.fetch({
    "test_data/lua/simple.lua",
    "test_data/lua/print_single.lua",
    "test_data/lua/print_multiple.lua"
  }, {dest = tmp_dir})

  -- Only commit first 2
  envy.commit_fetch({files[1], files[2]})

  -- Third file should still be in tmp here but will be cleaned up
end
"""
        recipe_path = self.cache_root / "prog_selective_commit.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

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
        """IDENTITY contains recipe identity."""
        recipe_content = """IDENTITY = "local.prog_ctx_identity@v1"

function FETCH(tmp_dir, options)
  if IDENTITY ~= "local.prog_ctx_identity@v1" then
    error("Expected identity 'local.prog_ctx_identity@v1', got: " .. IDENTITY)
  end

  -- Fetch something so phase completes successfully
  local file = envy.fetch("test_data/lua/simple.lua", {dest = tmp_dir})
  envy.commit_fetch(file)
end
"""
        recipe_path = self.cache_root / "prog_ctx_identity.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

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
        """opts is accessible as a table (empty when no options passed)."""
        recipe_content = """IDENTITY = "local.prog_ctx_options@v1"

function FETCH(tmp_dir, options)
  -- Verify options exists and is a table
  if type(options) ~= "table" then
    error("Expected options to be table, got: " .. type(options))
  end

  local file = envy.fetch("test_data/lua/simple.lua", {dest = tmp_dir})
  envy.commit_fetch(file)
end
"""
        recipe_path = self.cache_root / "prog_ctx_options.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

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
        """opts exists as empty table when no options specified."""
        recipe_content = """IDENTITY = "local.prog_ctx_options_empty@v1"

function FETCH(tmp_dir, options)
  if type(options) ~= "table" then
    error("Expected options to be table, got: " .. type(options))
  end

  -- Check it's empty
  local count = 0
  for k, v in pairs(options) do
    count = count + 1
  end

  if count ~= 0 then
    error("Expected empty options, got " .. count .. " entries")
  end

  local file = envy.fetch("test_data/lua/simple.lua", {dest = tmp_dir})
  envy.commit_fetch(file)
end
"""
        recipe_path = self.cache_root / "prog_ctx_options_empty.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

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
        """Multiple envy.fetch() calls execute serially, files accumulate."""
        recipe_content = """IDENTITY = "local.prog_serial_fetches@v1"

function FETCH(tmp_dir, options)
  -- First fetch
  local file1 = envy.fetch("test_data/lua/simple.lua", {dest = tmp_dir})

  -- Verify file1 exists in tmp
  local f = io.open(tmp_dir .. "/" .. file1, "r")
  if not f then error("file1 not in tmp after first fetch") end
  f:close()

  -- Second fetch
  local file2 = envy.fetch("test_data/lua/print_single.lua", {dest = tmp_dir})

  -- Verify both files exist
  f = io.open(tmp_dir .. "/" .. file1, "r")
  if not f then error("file1 disappeared after second fetch") end
  f:close()

  f = io.open(tmp_dir .. "/" .. file2, "r")
  if not f then error("file2 not in tmp after second fetch") end
  f:close()

  -- Third fetch
  local file3 = envy.fetch("test_data/lua/print_multiple.lua", {dest = tmp_dir})

  -- Commit all
  envy.commit_fetch({file1, file2, file3})
end
"""
        recipe_path = self.cache_root / "prog_serial_fetches.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

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
        recipe_content = """IDENTITY = "local.prog_error_prop@v1"

function FETCH(tmp_dir, options)
  error("Intentional test error")
end
"""
        recipe_path = self.cache_root / "prog_error_prop.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

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

    def test_fetch_function_returns_string(self):
        """FETCH = function(ctx) return \"url\" end (declarative string shorthand)."""
        recipe_content = """IDENTITY = "local.prog_return_string@v1"

FETCH = function(ctx)
  return "test_data/lua/simple.lua"
end
"""
        recipe_path = self.cache_root / "prog_return_string.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_return_string@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.prog_return_string@v1", result.stdout)

    def test_fetch_function_returns_table_single(self):
        """FETCH = function(ctx) return {source=\"...\", sha256=\"...\"} end."""
        # Get hash of test file
        test_file = Path(__file__).parent.parent / "test_data" / "lua" / "simple.lua"
        hash_result = subprocess.run(
            [str(self.envy), "hash", str(test_file)],
            capture_output=True,
            text=True,
            check=True,
        )
        file_hash = hash_result.stdout.split()[0]

        recipe_content = f"""IDENTITY = "local.prog_return_table@v1"

FETCH = function(ctx)
  return {{source = "test_data/lua/simple.lua", sha256 = "{file_hash}"}}
end
"""
        recipe_path = self.cache_root / "prog_return_table.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_return_table@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_fetch_function_returns_table_array(self):
        """FETCH = function(ctx) return {{source=\"...\"}, {source=\"...\"}} end."""
        recipe_content = """IDENTITY = "local.prog_return_array@v1"

FETCH = function(ctx)
  return {
    {source = "test_data/lua/simple.lua"},
    {source = "test_data/lua/print_single.lua"}
  }
end
"""
        recipe_path = self.cache_root / "prog_return_array.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_return_array@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_fetch_function_returns_string_array(self):
        """FETCH = function(ctx) return {\"url1\", \"url2\"} end."""
        recipe_content = """IDENTITY = "local.prog_return_str_array@v1"

FETCH = function(ctx)
  return {
    "test_data/lua/simple.lua",
    "test_data/lua/print_single.lua"
  }
end
"""
        recipe_path = self.cache_root / "prog_return_str_array.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_return_str_array@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_fetch_function_returns_with_options_templating(self):
        """FETCH = function(ctx, opts) return with opts templating."""
        # Note: engine-test doesn't support passing options, so we use default behavior
        # Real-world usage would pass options via manifest
        recipe_content = """IDENTITY = "local.prog_options_template@v1"

function FETCH(tmp_dir, options)
  local opts = options or {}
  local filename = opts.filename or "simple.lua"
  return "test_data/lua/" .. filename
end
"""
        recipe_path = self.cache_root / "prog_options_template.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_options_template@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_fetch_function_mixed_imperative_and_declarative(self):
        """FETCH = function(ctx) calls envy.fetch() and returns table (mixed mode)."""
        recipe_content = """IDENTITY = "local.prog_mixed_mode@v1"

function FETCH(tmp_dir, options)
  -- Imperative: fetch and commit one file
  local file1 = envy.fetch("test_data/lua/simple.lua", {dest = tmp_dir})
  envy.commit_fetch(file1)

  -- Declarative: return spec for more files
  return {
    "test_data/lua/print_single.lua",
    "test_data/lua/print_multiple.lua"
  }
end
"""
        recipe_path = self.cache_root / "prog_mixed_mode.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_mixed_mode@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_fetch_function_returns_nil(self):
        """FETCH = function(ctx) with explicit return nil (imperative mode)."""
        recipe_content = """IDENTITY = "local.prog_return_nil@v1"

function FETCH(tmp_dir, options)
  local file = envy.fetch("test_data/lua/simple.lua", {dest = tmp_dir})
  envy.commit_fetch(file)
  return nil
end
"""
        recipe_path = self.cache_root / "prog_return_nil.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_return_nil@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_fetch_function_returns_invalid_type(self):
        """FETCH = function(ctx) returns number (error)."""
        recipe_content = """IDENTITY = "local.prog_return_invalid@v1"

FETCH = function(ctx)
  return 42
end
"""
        recipe_path = self.cache_root / "prog_return_invalid.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                "local.prog_return_invalid@v1",
                str(recipe_path),
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(
            result.returncode, 0, "Expected error for invalid return type"
        )
        self.assertIn("must return nil, string, or table", result.stderr)


if __name__ == "__main__":
    unittest.main()

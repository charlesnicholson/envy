#!/usr/bin/env python3
"""Functional tests for engine declarative fetch per-file caching.

Tests package cache management: per-file caching across partial failures,
corruption detection/recovery, and SHA256-based revalidation.
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest

from . import test_config
from .trace_parser import TraceParser


class TestEngineFetchCaching(unittest.TestCase):
    """Tests for per-file package caching and recovery."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-engine-test-"))
        self.envy_test = test_config.get_envy_executable()
        self.envy = test_config.get_envy_executable()
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

    def test_declarative_fetch_partial_failure_then_complete(self):
        """Partial failure caches successful files, completion reuses them (no intrusive code)."""
        # Use shared cache root so second run sees first run's cached files
        shared_cache = Path(tempfile.mkdtemp(prefix="envy-shared-cache-"))

        try:
            # Create temp directory for the missing file
            temp_dir = shared_cache / "temp_files"
            temp_dir.mkdir(parents=True, exist_ok=True)
            missing_file = temp_dir / "fetch_partial_missing.lua"

            # Compute hashes dynamically
            simple_hash = self.get_file_hash("test_data/lua/simple.lua")
            print_single_hash = self.get_file_hash("test_data/lua/print_single.lua")

            # Create empty file and compute its hash
            empty_temp = shared_cache / "empty_temp.txt"
            empty_temp.write_text("")
            empty_hash = self.get_file_hash(empty_temp)

            # Create spec with computed hashes
            # Convert path to POSIX format (forward slashes) for Lua string compatibility
            temp_dir_posix = temp_dir.as_posix()
            spec_content = f"""-- Test per-file caching across partial failures
-- Two files succeed, one fails, then completion reuses cached files
IDENTITY = "local.fetch_partial@v1"

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
    -- This file will be created by the test after first run
    source = "file://{temp_dir_posix}/fetch_partial_missing.lua",
    sha256 = "{empty_hash}"
  }}
}}
"""
            modified_spec = shared_cache / "fetch_partial_modified.lua"
            modified_spec.write_text(spec_content)

            # Run 1: Partial failure (2 succeed, 1 fails - missing file doesn't exist yet)
            trace_file1 = shared_cache / "trace1.jsonl"
            result1 = subprocess.run(
                [
                    str(self.envy_test),
                    f"--cache-root={shared_cache}",
                    f"--trace=file:{trace_file1}",
                    "engine-test",
                    "local.fetch_partial@v1",
                    str(modified_spec),
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result1.returncode, 0, "Expected partial failure")
            self.assertIn(
                "fetch failed",
                result1.stderr.lower(),
                f"Expected fetch failure: {result1.stderr}",
            )

            # Verify fetch_dir has the 2 successful files (in package cache, not spec cache)
            # With hierarchical structure, find variant dirs under identity dir
            identity_dir = shared_cache / "packages" / "local.fetch_partial@v1"
            self.assertTrue(
                identity_dir.exists(), f"Identity dir should exist: {identity_dir}"
            )
            variant_dirs = list(identity_dir.glob("*-blake3-*"))
            self.assertEqual(
                len(variant_dirs), 1, f"Expected 1 variant dir, found: {variant_dirs}"
            )
            fetch_dir = variant_dirs[0] / "fetch"
            self.assertTrue(
                (fetch_dir / "simple.lua").exists(), "simple.lua should be cached"
            )
            self.assertTrue(
                (fetch_dir / "print_single.lua").exists(),
                "print_single.lua should be cached",
            )

            # Create the missing file (empty file matches the SHA256 for empty content)
            missing_file.write_text("")

            # Run 2: Completion with cache - use same cache root
            trace_file2 = shared_cache / "trace2.jsonl"
            result2 = subprocess.run(
                [
                    str(self.envy_test),
                    f"--cache-root={shared_cache}",
                    f"--trace=file:{trace_file2}",
                    "engine-test",
                    "local.fetch_partial@v1",
                    str(modified_spec),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                result2.returncode, 0, f"Second run failed: {result2.stderr}"
            )

            # Verify caching behavior via structured trace
            parser2 = TraceParser(trace_file2)
            all_events2 = parser2.parse()

            # Verify trace events were generated
            self.assertGreater(
                len(all_events2), 0, "Expected trace events on second run"
            )

            # The test verifies that 2 files were cached and reused
            # We confirm this by checking that only 1 file was downloaded (the missing one)
            # This demonstrates the other 2 files were successfully reused from cache
            self.assertIn(
                "downloading 1 file(s)",
                result2.stderr,
                f"Expected to download only the missing file (cached files reused): {result2.stderr}",
            )
        finally:
            shutil.rmtree(shared_cache, ignore_errors=True)

    def test_declarative_fetch_intrusive_partial_failure(self):
        """Use --fail-after-fetch-count to simulate partial download (intrusive flag)."""
        # Use shared cache root so second run sees first run's cached files
        shared_cache = Path(tempfile.mkdtemp(prefix="envy-intrusive-cache-"))

        try:
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
            modified_spec = shared_cache / "fetch_array.lua"
            modified_spec.write_text(spec_content)

            # Run 1: Download 2 files then fail
            result1 = subprocess.run(
                [
                    str(self.envy_test),
                    f"--cache-root={shared_cache}",
                    "--trace",
                    "engine-test",
                    "local.fetch_array@v1",  # Has 3 files
                    str(modified_spec),
                    "--fail-after-fetch-count=2",
                ],
                capture_output=True,
                text=True,
            )

            self.assertNotEqual(
                result1.returncode, 0, "Expected failure after 2 downloads"
            )
            self.assertIn(
                "fail_after_fetch_count",
                result1.stderr.lower(),
                f"Expected fail_after_fetch_count error: {result1.stderr}",
            )

            # Verify fetch_dir has the first 2 files cached (in package cache, not spec cache)
            # With hierarchical structure, find variant dirs under identity dir
            identity_dir = shared_cache / "packages" / "local.fetch_array@v1"
            self.assertTrue(
                identity_dir.exists(), f"Identity dir should exist: {identity_dir}"
            )
            variant_dirs = list(identity_dir.glob("*-blake3-*"))
            self.assertEqual(
                len(variant_dirs), 1, f"Expected 1 variant dir, found: {variant_dirs}"
            )
            fetch_dir = variant_dirs[0] / "fetch"
            # Check that at least some files exist (order may vary due to concurrent downloads)
            cached_files = list(fetch_dir.glob("*.lua")) if fetch_dir.exists() else []
            self.assertGreaterEqual(
                len(cached_files),
                2,
                f"Expected at least 2 cached files, got: {cached_files}",
            )

            # Run 2: Complete without flag - use same cache root
            result2 = subprocess.run(
                [
                    str(self.envy_test),
                    f"--cache-root={shared_cache}",
                    "--trace",
                    "engine-test",
                    "local.fetch_array@v1",
                    str(modified_spec),
                ],
                capture_output=True,
                text=True,
            )

            self.assertEqual(
                result2.returncode, 0, f"Second run failed: {result2.stderr}"
            )

            # Verify cache hits for some files
            stderr_lower = result2.stderr.lower()
            self.assertIn(
                "cache hit", stderr_lower, f"Expected cache hit log: {result2.stderr}"
            )
            # Verify only remaining file(s) downloaded
            self.assertIn(
                "downloading 1 file(s)",
                result2.stderr,
                f"Expected 1 download in second run: {result2.stderr}",
            )
        finally:
            shutil.rmtree(shared_cache, ignore_errors=True)

    def test_declarative_fetch_corrupted_cache(self):
        """Corrupted files in fetch/ are detected and re-downloaded."""
        shared_cache = Path(tempfile.mkdtemp(prefix="envy-corrupted-cache-"))

        try:
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
            modified_spec = shared_cache / "fetch_array.lua"
            modified_spec.write_text(spec_content)

            identity_dir = shared_cache / "packages" / "local.fetch_array@v1"

            # Run 1: Let it create the structure but fail it
            result_setup = subprocess.run(
                [
                    str(self.envy_test),
                    f"--cache-root={shared_cache}",
                    "--trace",
                    "engine-test",
                    "local.fetch_array@v1",
                    str(modified_spec),
                    "--fail-after-fetch-count=1",  # Fail after 1 file
                ],
                capture_output=True,
                text=True,
            )
            # Should fail
            self.assertNotEqual(result_setup.returncode, 0)

            # Now find the fetch directory and corrupt one of the files
            variant_dirs = list(identity_dir.glob("*-blake3-*"))
            self.assertEqual(
                len(variant_dirs), 1, f"Expected 1 variant dir: {variant_dirs}"
            )
            fetch_dir = variant_dirs[0] / "fetch"

            # Corrupt simple.lua (replace with garbage that won't match SHA256)
            corrupted_file = fetch_dir / "simple.lua"
            corrupted_file.write_text(
                "GARBAGE CONTENT THAT WILL FAIL SHA256 VERIFICATION"
            )

            # Run 2: Should detect corruption and re-download
            result = subprocess.run(
                [
                    str(self.envy_test),
                    f"--cache-root={shared_cache}",
                    "--trace",
                    "engine-test",
                    "local.fetch_array@v1",
                    str(modified_spec),
                ],
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 0, f"Should succeed: {result.stderr}")

            # Verify that corruption was detected and file re-downloaded
            stderr_lower = result.stderr.lower()
            self.assertIn(
                "cache mismatch",
                stderr_lower,
                f"Expected cache mismatch detection: {result.stderr}",
            )

            # Verify package completed successfully (entry-level marker exists)
            # Note: fetch/ is deleted after successful package completion
            entry_complete = variant_dirs[0] / "envy-complete"
            self.assertTrue(
                entry_complete.exists(),
                "Entry-level completion marker should exist after successful package install",
            )

            # Verify package directory exists with installed files
            pkg_dir = variant_dirs[0] / "pkg"
            self.assertTrue(
                pkg_dir.exists(), "Package directory should exist after completion"
            )

        finally:
            shutil.rmtree(shared_cache, ignore_errors=True)

    def test_declarative_fetch_complete_but_unmarked(self):
        """All files present with correct SHA256, but no completion marker."""
        shared_cache = Path(tempfile.mkdtemp(prefix="envy-complete-unmarked-"))

        try:
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
            modified_spec = shared_cache / "fetch_array.lua"
            modified_spec.write_text(spec_content)

            identity_dir = shared_cache / "packages" / "local.fetch_array@v1"

            # Run once to establish cache structure, then fail it
            result_setup = subprocess.run(
                [
                    str(self.envy_test),
                    f"--cache-root={shared_cache}",
                    "--trace",
                    "engine-test",
                    "local.fetch_array@v1",
                    str(modified_spec),
                    "--fail-after-fetch-count=1",
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result_setup.returncode, 0)

            # Find fetch directory
            variant_dirs = list(identity_dir.glob("*-blake3-*"))
            self.assertEqual(len(variant_dirs), 1)
            fetch_dir = variant_dirs[0] / "fetch"
            fetch_dir.mkdir(parents=True, exist_ok=True)

            # Copy actual test files to cache (they'll match the computed hashes)
            shutil.copy("test_data/lua/simple.lua", fetch_dir / "simple.lua")
            shutil.copy(
                "test_data/lua/print_single.lua", fetch_dir / "print_single.lua"
            )
            shutil.copy(
                "test_data/lua/print_multiple.lua", fetch_dir / "print_multiple.lua"
            )

            # Ensure NO completion marker exists
            completion_marker = fetch_dir / "envy-complete"
            if completion_marker.exists():
                completion_marker.unlink()

            # Run: Should verify cached files by SHA256, reuse them
            result = subprocess.run(
                [
                    str(self.envy_test),
                    f"--cache-root={shared_cache}",
                    "--trace",
                    "engine-test",
                    "local.fetch_array@v1",
                    str(modified_spec),
                ],
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 0, f"Should succeed: {result.stderr}")

            stderr_lower = result.stderr.lower()

            # Should see cache hits for files with SHA256
            self.assertIn(
                "cache hit",
                stderr_lower,
                f"Expected cache hits for verified files: {result.stderr}",
            )

            # Should still download print_multiple.lua (no SHA256 = can't trust)
            self.assertIn(
                "downloading",
                stderr_lower,
                f"Expected download for file without SHA256: {result.stderr}",
            )

            # Verify package completed successfully (entry-level marker exists)
            # Note: fetch/ and its marker are deleted after successful package completion
            entry_complete = variant_dirs[0] / "envy-complete"
            self.assertTrue(
                entry_complete.exists(),
                "Entry-level completion marker should exist after successful package install",
            )

        finally:
            shutil.rmtree(shared_cache, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()

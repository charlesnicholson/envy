#!/usr/bin/env python3
"""Functional tests for engine phase execution.

Tests the phase lifecycle: check(), fetch(), and install() phase execution
with proper trace logging and dependency handling.
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest

from . import test_config
from .trace_parser import PkgPhase, TraceParser

# Inline specs for phase execution tests
SPECS = {
    "simple.lua": '''-- Minimal test spec - no dependencies
IDENTITY = "local.simple@v1"
DEPENDENCIES = {}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package - no cache interaction
end
''',
    "fetch_function_basic.lua": '''-- Test spec with basic fetch function
IDENTITY = "local.fetcher@v1"
DEPENDENCIES = {}

function FETCH(tmp_dir, options)
  -- Simulates fetching by writing a test file
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Install from fetched materials
end
''',
    "fetch_function_with_dep.lua": '''-- Test spec with fetch function that depends on another recipe
IDENTITY = "local.fetcher_with_dep@v1"
DEPENDENCIES = {
  { spec = "local.tool@v1", source = "tool.lua" }
}

function FETCH(tmp_dir, options)
  -- Fetch phase uses a dependency
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Install from fetched materials
end
''',
    "tool.lua": '''-- Minimal tool spec used as dependency
IDENTITY = "local.tool@v1"
DEPENDENCIES = {}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Install tool
end
''',
}


class TestEnginePhases(unittest.TestCase):
    """Tests for phase execution lifecycle."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-engine-test-"))
        self.specs_dir = Path(tempfile.mkdtemp(prefix="envy-engine-specs-"))
        self.envy_test = test_config.get_envy_executable()
        self.envy = test_config.get_envy_executable()

        # Write inline specs to temp directory
        for name, content in SPECS.items():
            (self.specs_dir / name).write_text(content, encoding="utf-8")

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.specs_dir, ignore_errors=True)

    def get_file_hash(self, filepath):
        """Get SHA256 hash of file using envy hash command."""
        result = subprocess.run(
            [str(self.envy), "hash", str(filepath)],
            capture_output=True,
            text=True,
            check=True,
        )
        return result.stdout.strip()

    def test_phase_execution_check_false(self):
        """Engine executes check() and install() phases with structured trace."""
        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.simple@v1",
                str(self.specs_dir / "simple.lua"),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Verify phase execution via structured trace
        parser = TraceParser(trace_file)
        phase_sequence = parser.get_phase_sequence("local.simple@v1")
        self.assertIn(PkgPhase.PKG_CHECK, phase_sequence)
        self.assertIn(PkgPhase.PKG_INSTALL, phase_sequence)

    def test_fetch_function_basic(self):
        """Engine executes fetch() phase for specs with fetch function."""
        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.fetcher@v1",
                str(self.specs_dir / "fetch_function_basic.lua"),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Verify fetch phase execution via structured trace
        parser = TraceParser(trace_file)
        phase_sequence = parser.get_phase_sequence("local.fetcher@v1")
        self.assertIn(PkgPhase.PKG_FETCH, phase_sequence)

        # Verify output contains package hash
        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 1)
        key, value = lines[0].split(" -> ", 1)
        self.assertEqual(key, "local.fetcher@v1")
        self.assertGreater(len(value), 0)

    def test_fetch_function_with_dependency(self):
        """Engine executes fetch() with dependencies available."""
        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.fetcher_with_dep@v1",
                str(self.specs_dir / "fetch_function_with_dep.lua"),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Verify both specs executed
        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 2)

        # Verify dependency executed
        dep_lines = [l for l in lines if "local.tool@v1" in l]
        self.assertEqual(len(dep_lines), 1)

        # Verify main spec executed
        main_lines = [l for l in lines if "local.fetcher_with_dep@v1" in l]
        self.assertEqual(len(main_lines), 1)

        # Verify dependency relationship via structured trace
        parser = TraceParser(trace_file)
        deps = parser.get_dependency_added_events("local.fetcher_with_dep@v1")
        dep_names = [d.raw.get("dependency") for d in deps]
        self.assertIn("local.tool@v1", dep_names)


if __name__ == "__main__":
    unittest.main()

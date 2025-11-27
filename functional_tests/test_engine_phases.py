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
from .trace_parser import RecipePhase, TraceParser


class TestEnginePhases(unittest.TestCase):
    """Tests for phase execution lifecycle."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-engine-test-"))
        self.envy_test = test_config.get_envy_executable()
        self.envy = test_config.get_envy_executable()

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

    def test_phase_execution_check_false(self):
        """Engine executes check() and install() phases with structured trace."""
        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.simple@v1",
                "test_data/recipes/simple.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Verify phase execution via structured trace
        parser = TraceParser(trace_file)
        phase_sequence = parser.get_phase_sequence("local.simple@v1")
        self.assertIn(RecipePhase.ASSET_CHECK, phase_sequence)
        self.assertIn(RecipePhase.ASSET_INSTALL, phase_sequence)

    def test_fetch_function_basic(self):
        """Engine executes fetch() phase for recipes with fetch function."""
        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.fetcher@v1",
                "test_data/recipes/fetch_function_basic.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Verify fetch phase execution via structured trace
        parser = TraceParser(trace_file)
        phase_sequence = parser.get_phase_sequence("local.fetcher@v1")
        self.assertIn(RecipePhase.ASSET_FETCH, phase_sequence)

        # Verify output contains asset hash
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
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.fetcher_with_dep@v1",
                "test_data/recipes/fetch_function_with_dep.lua",
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Verify both recipes executed
        lines = [line for line in result.stdout.strip().split("\n") if line]
        self.assertEqual(len(lines), 2)

        # Verify dependency executed
        dep_lines = [l for l in lines if "local.tool@v1" in l]
        self.assertEqual(len(dep_lines), 1)

        # Verify main recipe executed
        main_lines = [l for l in lines if "local.fetcher_with_dep@v1" in l]
        self.assertEqual(len(main_lines), 1)

        # Verify dependency relationship via structured trace
        parser = TraceParser(trace_file)
        deps = parser.get_dependency_added_events("local.fetcher_with_dep@v1")
        dep_names = [d.raw.get("dependency") for d in deps]
        self.assertIn("local.tool@v1", dep_names)


if __name__ == "__main__":
    unittest.main()

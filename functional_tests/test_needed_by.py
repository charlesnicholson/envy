#!/usr/bin/env python3
"""Functional tests for needed_by phase dependencies.

Tests that needed_by annotation enables fine-grained parallelism by allowing
specs to specify which phase they actually need a dependency for. For example,
A depends on B with needed_by="build" means A's fetch/stage can run in parallel
with B's pipeline, blocking only when A needs to build.
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest

from . import test_config
from .trace_parser import PkgPhase, TraceParser


class TestNeededBy(unittest.TestCase):
    """Tests for needed_by phase coupling."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-needed-by-test-"))
        self.envy_test = test_config.get_envy_executable()

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)

    def test_needed_by_fetch_allows_parallelism(self):
        """Spec A depends on B with needed_by='fetch' - A's early phases run in parallel."""
        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.needed_by_fetch_parent@v1",
                "test_data/specs/needed_by_fetch_parent.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.needed_by_fetch_parent@v1", result.stdout)
        self.assertIn("local.needed_by_fetch_dep@v1", result.stdout)

        # Verify needed_by phase is set correctly
        parser = TraceParser(trace_file)
        parser.assert_dependency_needed_by(
            "local.needed_by_fetch_parent@v1",
            "local.needed_by_fetch_dep@v1",
            PkgPhase.ASSET_FETCH,
        )

    def test_needed_by_build(self):
        """Spec A depends on B with needed_by='build' - fetch/stage parallel, build waits."""
        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.needed_by_build_parent@v1",
                "test_data/specs/needed_by_build_parent.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.needed_by_build_parent@v1", result.stdout)
        self.assertIn("local.needed_by_build_dep@v1", result.stdout)

        parser = TraceParser(trace_file)
        parser.assert_dependency_needed_by(
            "local.needed_by_build_parent@v1",
            "local.needed_by_build_dep@v1",
            PkgPhase.ASSET_BUILD,
        )

    def test_needed_by_stage(self):
        """Spec A depends on B with needed_by='stage' - fetch parallel, stage waits."""
        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.needed_by_stage_parent@v1",
                "test_data/specs/needed_by_stage_parent.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.needed_by_stage_parent@v1", result.stdout)
        self.assertIn("local.needed_by_stage_dep@v1", result.stdout)

        parser = TraceParser(trace_file)
        parser.assert_dependency_needed_by(
            "local.needed_by_stage_parent@v1",
            "local.needed_by_stage_dep@v1",
            PkgPhase.ASSET_STAGE,
        )

    def test_needed_by_install(self):
        """Spec A depends on B with needed_by='install' - fetch/stage/build parallel, install waits."""
        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.needed_by_install_parent@v1",
                "test_data/specs/needed_by_install_parent.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.needed_by_install_parent@v1", result.stdout)
        self.assertIn("local.needed_by_install_dep@v1", result.stdout)

        parser = TraceParser(trace_file)
        parser.assert_dependency_needed_by(
            "local.needed_by_install_parent@v1",
            "local.needed_by_install_dep@v1",
            PkgPhase.ASSET_INSTALL,
        )

    def test_needed_by_deploy(self):
        """Spec A depends on B with needed_by='deploy' - all phases parallel except deploy."""
        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.needed_by_deploy_parent@v1",
                "test_data/specs/needed_by_deploy_parent.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.needed_by_deploy_parent@v1", result.stdout)
        self.assertIn("local.needed_by_deploy_dep@v1", result.stdout)

        parser = TraceParser(trace_file)
        parser.assert_dependency_needed_by(
            "local.needed_by_deploy_parent@v1",
            "local.needed_by_deploy_dep@v1",
            PkgPhase.ASSET_DEPLOY,
        )

    def test_needed_by_check(self):
        """Spec A depends on B with needed_by='check' - check waits, rest runs parallel."""
        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.needed_by_check_parent@v1",
                "test_data/specs/needed_by_check_parent.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.needed_by_check_parent@v1", result.stdout)
        self.assertIn("local.needed_by_check_dep@v1", result.stdout)

        parser = TraceParser(trace_file)
        parser.assert_dependency_needed_by(
            "local.needed_by_check_parent@v1",
            "local.needed_by_check_dep@v1",
            PkgPhase.ASSET_CHECK,
        )

    def test_needed_by_default_to_build(self):
        """Spec A depends on B without needed_by - defaults to build phase."""
        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.needed_by_default_parent@v1",
                "test_data/specs/needed_by_default_parent.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.needed_by_default_parent@v1", result.stdout)
        self.assertIn("local.dep_val_lib@v1", result.stdout)

        parser = TraceParser(trace_file)
        parser.assert_dependency_needed_by(
            "local.needed_by_default_parent@v1",
            "local.dep_val_lib@v1",
            PkgPhase.ASSET_BUILD,
        )

    def test_needed_by_invalid_phase_name(self):
        """Spec with needed_by='nonexistent' fails during parsing."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                "engine-test",
                "local.needed_by_invalid@v1",
                "test_data/specs/needed_by_invalid.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(result.returncode, 0, "Expected invalid phase name to fail")
        # Error message check - keep stderr check for error messages
        self.assertIn("needed_by", result.stderr.lower())

    def test_needed_by_multi_level_chain(self):
        """Multi-level chain: A→B→C with different needed_by phases."""
        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.needed_by_chain_a@v1",
                "test_data/specs/needed_by_chain_a.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.needed_by_chain_a@v1", result.stdout)
        self.assertIn("local.needed_by_chain_b@v1", result.stdout)
        self.assertIn("local.needed_by_chain_c@v1", result.stdout)

        # Verify all three specs completed successfully
        parser = TraceParser(trace_file)
        for spec in [
            "local.needed_by_chain_a@v1",
            "local.needed_by_chain_b@v1",
            "local.needed_by_chain_c@v1",
        ]:
            completes = parser.filter_by_spec_and_event(spec, "phase_complete")
            self.assertGreater(
                len(completes), 0, f"Expected {spec} to complete phases"
            )

    def test_needed_by_diamond(self):
        """Diamond: A depends on B+C with different needed_by phases."""
        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.needed_by_diamond_a@v1",
                "test_data/specs/needed_by_diamond_a.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.needed_by_diamond_a@v1", result.stdout)
        self.assertIn("local.needed_by_diamond_b@v1", result.stdout)
        self.assertIn("local.needed_by_diamond_c@v1", result.stdout)

        # Verify all three specs completed
        parser = TraceParser(trace_file)
        for spec in [
            "local.needed_by_diamond_a@v1",
            "local.needed_by_diamond_b@v1",
            "local.needed_by_diamond_c@v1",
        ]:
            completes = parser.filter_by_spec_and_event(spec, "phase_complete")
            self.assertGreater(
                len(completes), 0, f"Expected {spec} to complete phases"
            )

    def test_needed_by_race_condition(self):
        """Dependency completes before parent discovers it - late edge addition handled."""
        # This tests the if (dep_acc->second.completed) { try_put() } logic
        # Run with parallel jobs to increase chance of race
        env = os.environ.copy()
        env["ENVY_TEST_JOBS"] = "8"

        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.needed_by_race_parent@v1",
                "test_data/specs/needed_by_race_parent.lua",
            ],
            capture_output=True,
            text=True,
            env=env,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.needed_by_race_parent@v1", result.stdout)

        parser = TraceParser(trace_file)
        completes = parser.filter_by_spec_and_event(
            "local.needed_by_race_parent@v1", "phase_complete"
        )
        self.assertGreater(len(completes), 0, "Expected parent to complete phases")

    def test_needed_by_with_cache_hit(self):
        """Spec with needed_by where dependency is already cached."""
        trace_file1 = self.cache_root / "trace1.jsonl"
        result1 = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file1}",
                "engine-test",
                "local.needed_by_cached_parent@v1",
                "test_data/specs/needed_by_cached_parent.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result1.returncode, 0, f"stderr: {result1.stderr}")

        # Verify first run had cache miss for dependency
        parser1 = TraceParser(trace_file1)
        cache_misses = parser1.filter_by_event("cache_miss")
        self.assertGreater(len(cache_misses), 0, "Expected cache misses on first run")

        # Second run: cache hit (should still respect needed_by)
        trace_file2 = self.cache_root / "trace2.jsonl"
        result2 = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file2}",
                "engine-test",
                "local.needed_by_cached_parent@v1",
                "test_data/specs/needed_by_cached_parent.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result2.returncode, 0, f"stderr: {result2.stderr}")
        self.assertIn("local.needed_by_cached_parent@v1", result2.stdout)

        # Verify second run had cache hits
        parser2 = TraceParser(trace_file2)
        cache_hits = parser2.filter_by_event("cache_hit")
        self.assertGreater(len(cache_hits), 0, "Expected cache hits on second run")

    def test_needed_by_all_phases(self):
        """Spec with multiple dependencies using different needed_by phases."""
        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.needed_by_all_phases@v1",
                "test_data/specs/needed_by_all_phases.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("local.needed_by_all_phases@v1", result.stdout)

        # Verify spec completed and has multiple dependencies
        parser = TraceParser(trace_file)
        deps = parser.get_dependency_added_events("local.needed_by_all_phases@v1")
        self.assertGreater(len(deps), 1, "Expected multiple dependencies")


if __name__ == "__main__":
    unittest.main()

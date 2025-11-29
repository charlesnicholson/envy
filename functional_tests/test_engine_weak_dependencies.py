#!/usr/bin/env python3
"""Functional tests for weak dependency resolution."""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest

from . import test_config


class TestEngineWeakDependencies(unittest.TestCase):
    """Weak dependency resolution scenarios."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-weak-ft-"))
        self.envy_test = test_config.get_envy_executable()
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)

    def run_engine(self, identity, recipe_path):
        return subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                identity,
                recipe_path,
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
        )

    @staticmethod
    def parse_output(stdout):
        lines = [line for line in stdout.strip().split("\n") if line]
        return dict(line.split(" -> ", 1) for line in lines)

    def test_weak_fallback_used_when_no_match(self):
        result = self.run_engine(
            "local.weak_consumer_fallback@v1",
            "test_data/recipes/weak_consumer_fallback.lua",
        )
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        output = self.parse_output(result.stdout)
        self.assertIn("local.weak_consumer_fallback@v1", output)
        self.assertIn("local.weak_fallback@v1", output)

    def test_weak_prefers_existing_match(self):
        result = self.run_engine(
            "local.weak_consumer_existing@v1",
            "test_data/recipes/weak_consumer_existing.lua",
        )
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        output = self.parse_output(result.stdout)
        self.assertIn("local.existing_dep@v1", output)
        self.assertNotIn("local.unused_fallback@v1", output)

    def test_reference_only_resolves_to_existing_recipe(self):
        result = self.run_engine(
            "local.weak_consumer_ref_only@v1",
            "test_data/recipes/weak_consumer_ref_only.lua",
        )
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        output = self.parse_output(result.stdout)
        self.assertIn("local.weak_provider@v1", output)

    def test_ambiguity_reports_all_candidates(self):
        result = self.run_engine(
            "local.weak_consumer_ambiguous@v1",
            "test_data/recipes/weak_consumer_ambiguous.lua",
        )
        self.assertNotEqual(result.returncode, 0, "Ambiguity should fail resolution")
        self.assertIn("ambiguous", result.stderr.lower())
        self.assertIn("local.dupe@v1", result.stderr)
        self.assertIn("local.dupe@v2", result.stderr)

    def test_missing_reference_reports_progress_error(self):
        result = self.run_engine(
            "local.weak_missing_ref@v1",
            "test_data/recipes/weak_missing_ref.lua",
        )
        self.assertNotEqual(result.returncode, 0, "Missing reference should fail")
        self.assertIn("never_provided", result.stderr)
        self.assertIn("no progress", result.stderr.lower())

    def test_cascading_weak_resolution(self):
        result = self.run_engine(
            "local.weak_chain_root@v1",
            "test_data/recipes/weak_chain_root.lua",
        )
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        output = self.parse_output(result.stdout)
        self.assertIn("local.chain_b@v1", output)
        self.assertIn("local.chain_c@v1", output)

    def test_progress_with_flat_unresolved_count(self):
        result = self.run_engine(
            "local.weak_progress_flat_root@v1",
            "test_data/recipes/weak_progress_flat_root.lua",
        )
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        output = self.parse_output(result.stdout)
        self.assertIn("local.branch_one@v1", output)
        self.assertIn("local.branch_two@v1", output)
        self.assertIn("local.shared@v1", output)


if __name__ == "__main__":
    unittest.main()


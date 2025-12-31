"""Tests for default needed_by phase behavior."""

import subprocess
import tempfile
import unittest
from pathlib import Path

from . import test_config
from .trace_parser import PkgPhase, TraceParser


class TestDefaultNeededBy(unittest.TestCase):
    """Tests verifying default needed_by phase is asset_build."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-needed-by-test-"))
        self.envy_test = test_config.get_envy_executable()

    def tearDown(self):
        import shutil

        shutil.rmtree(self.cache_root, ignore_errors=True)

    def test_default_needed_by_is_build_not_check(self):
        """Verify default needed_by is asset_build (phase 4), not asset_check."""
        trace_file = self.cache_root / "trace.jsonl"

        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.default_needed_by_parent@v1",
                "test_data/specs/default_needed_by_parent.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertTrue(trace_file.exists(), "Trace file not created")

        parser = TraceParser(trace_file)

        # Verify dependency was added with correct needed_by phase
        parser.assert_dependency_needed_by(
            "local.default_needed_by_parent@v1",
            "local.dep_val_lib@v1",
            PkgPhase.ASSET_BUILD,
        )

        # Verify phase sequence - parent should execute through build phase
        parent_sequence = parser.get_phase_sequence("local.default_needed_by_parent@v1")
        self.assertIn(
            PkgPhase.ASSET_BUILD,
            parent_sequence,
            "Parent should reach build phase where dependency is needed",
        )

    def test_explicit_needed_by_check_still_works(self):
        """Verify explicit needed_by='check' still works correctly."""
        trace_file = self.cache_root / "trace.jsonl"

        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.explicit_check_parent@v1",
                "test_data/specs/explicit_check_parent.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertTrue(trace_file.exists(), "Trace file not created")

        parser = TraceParser(trace_file)

        # Verify dependency was added with explicit needed_by=check (phase 1)
        parser.assert_dependency_needed_by(
            "local.explicit_check_parent@v1",
            "local.dep_val_lib@v1",
            PkgPhase.ASSET_CHECK,
        )

    def test_explicit_needed_by_fetch_works(self):
        """Verify explicit needed_by='fetch' works correctly."""
        trace_file = self.cache_root / "trace.jsonl"

        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.explicit_fetch_parent@v1",
                "test_data/specs/explicit_fetch_parent.lua",
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertTrue(trace_file.exists(), "Trace file not created")

        parser = TraceParser(trace_file)

        # Verify dependency was added with explicit needed_by=fetch (phase 2)
        parser.assert_dependency_needed_by(
            "local.explicit_fetch_parent@v1",
            "local.dep_val_lib@v1",
            PkgPhase.ASSET_FETCH,
        )


if __name__ == "__main__":
    unittest.main()

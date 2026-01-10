#!/usr/bin/env python3
"""Smoke tests for structured trace output.

Tests the trace infrastructure works correctly with different output modes:
- Human-readable stderr output
- Structured JSONL file output
- Multiple outputs simultaneously
"""

import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from . import test_config
from .trace_parser import TraceParser

SIMPLE_SPEC = '''IDENTITY = "local.simple@v1"
DEPENDENCIES = {}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
'''


class TestStructuredTrace(unittest.TestCase):
    """Smoke tests for structured logging infrastructure."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-trace-smoke-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-trace-specs-"))
        self.envy = test_config.get_envy_executable()

        # Write spec to temp directory
        self.spec_path = self.test_dir / "simple.lua"
        self.spec_path.write_text(SIMPLE_SPEC, encoding="utf-8")

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_trace_stderr_human_readable(self):
        """Verify --trace=stderr produces human-readable output."""
        result = subprocess.run(
            [
                str(self.envy),
                f"--cache-root={self.cache_root}",
                "--trace=stderr",
                "engine-test",
                "local.simple@v1",
                str(self.spec_path),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Verify human-readable trace output in stderr
        # Should NOT be JSON - should be human-readable text
        stderr_lines = result.stderr.strip().split("\n")
        self.assertGreater(len(stderr_lines), 0, "Expected trace output in stderr")

        # Check that it's NOT JSON (human-readable format)
        # Human-readable lines typically start with timestamps or log levels
        # JSON lines would start with '{'
        non_json_lines = [
            line for line in stderr_lines if not line.strip().startswith("{")
        ]
        self.assertGreater(
            len(non_json_lines), 0, "Expected human-readable (non-JSON) output"
        )

    def test_trace_file_jsonl_output(self):
        """Verify --trace=file:<path> produces valid JSONL."""
        trace_file = self.cache_root / "trace.jsonl"

        result = subprocess.run(
            [
                str(self.envy),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.simple@v1",
                str(self.spec_path),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertTrue(trace_file.exists(), "Trace file should be created")

        # Verify JSONL format
        parser = TraceParser(trace_file)
        events = parser.parse()

        self.assertGreater(len(events), 0, "Expected trace events in file")

        # Verify each line is valid JSON
        with open(trace_file) as f:
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    event = json.loads(line)
                    # Verify required fields
                    self.assertIn("ts", event, f"Line {line_num}: Missing 'ts' field")
                    self.assertIn(
                        "event", event, f"Line {line_num}: Missing 'event' field"
                    )
                except json.JSONDecodeError as e:
                    self.fail(f"Line {line_num} is not valid JSON: {e}\nLine: {line}")

    def test_trace_multiple_outputs_simultaneously(self):
        """Verify --trace=stderr,file:<path> works simultaneously."""
        trace_file = self.cache_root / "trace.jsonl"

        result = subprocess.run(
            [
                str(self.envy),
                f"--cache-root={self.cache_root}",
                f"--trace=stderr,file:{trace_file}",
                "engine-test",
                "local.simple@v1",
                str(self.spec_path),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Verify stderr has human-readable output
        stderr_lines = result.stderr.strip().split("\n")
        self.assertGreater(len(stderr_lines), 0, "Expected trace output in stderr")

        # Verify file has JSONL output
        self.assertTrue(trace_file.exists(), "Trace file should be created")
        parser = TraceParser(trace_file)
        events = parser.parse()
        self.assertGreater(len(events), 0, "Expected trace events in file")

    def test_trace_file_contains_expected_events(self):
        """Verify trace file contains expected event types."""
        trace_file = self.cache_root / "trace.jsonl"

        result = subprocess.run(
            [
                str(self.envy),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.simple@v1",
                str(self.spec_path),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        parser = TraceParser(trace_file)
        events = parser.parse()

        # Verify we have expected event types
        event_types = set(e.event for e in events)

        # Should have at least these core event types
        expected_types = {
            "spec_registered",
            "phase_start",
            "phase_complete",
        }

        for expected_type in expected_types:
            self.assertIn(
                expected_type, event_types, f"Expected '{expected_type}' event in trace"
            )

    def test_trace_disabled_by_default(self):
        """Verify trace is not output when --trace flag not provided."""
        trace_file = self.cache_root / "trace.jsonl"

        # Run WITHOUT --trace flag
        result = subprocess.run(
            [
                str(self.envy),
                f"--cache-root={self.cache_root}",
                "engine-test",
                "local.simple@v1",
                str(self.spec_path),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Trace file should NOT exist
        self.assertFalse(
            trace_file.exists(), "Trace file should not be created without --trace flag"
        )

        # Stderr should not contain trace events (may have debug/info/warn/error, but not structured trace)
        # This is harder to verify precisely, but we can check there's no JSON-like output
        if result.stderr:
            stderr_lines = [
                line.strip() for line in result.stderr.split("\n") if line.strip()
            ]
            json_lines = [line for line in stderr_lines if line.startswith("{")]
            self.assertEqual(
                len(json_lines),
                0,
                "Should not have JSON trace output without --trace flag",
            )


if __name__ == "__main__":
    unittest.main()

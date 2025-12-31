"""Unit tests for trace_parser module."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from typing import Any

from .trace_parser import PkgPhase, TraceParser


def create_trace_file(events: list[dict[str, Any]]) -> Path:
    """Helper to create a temporary trace file with given events."""
    tmpfile = tempfile.NamedTemporaryFile(mode="w", suffix=".jsonl", delete=False)
    for event in events:
        json.dump(event, tmpfile)
        tmpfile.write("\n")
    tmpfile.close()
    return Path(tmpfile.name)


class TestTraceParser(unittest.TestCase):
    """Tests for trace_parser module."""

    def test_parse_empty_file(self):
        """Test parsing an empty trace file."""
        trace_file = create_trace_file([])
        parser = TraceParser(trace_file)

        events = parser.parse()
        self.assertEqual(len(events), 0)

        trace_file.unlink()

    def test_parse_single_event(self):
        """Test parsing a single trace event."""
        trace_file = create_trace_file(
            [
                {
                    "ts": "2025-01-15T10:30:00.123Z",
                    "event": "phase_start",
                    "spec": "test@v1",
                    "phase": "recipe_fetch",
                    "phase_num": 0,
                }
            ]
        )
        parser = TraceParser(trace_file)

        events = parser.parse()
        self.assertEqual(len(events), 1)

        event = events[0]
        self.assertEqual(event.event, "phase_start")
        self.assertEqual(event.ts, "2025-01-15T10:30:00.123Z")
        self.assertEqual(event.spec, "test@v1")
        self.assertEqual(event.phase, PkgPhase.SPEC_FETCH)

        trace_file.unlink()

    def test_parse_multiple_events(self):
        """Test parsing multiple trace events."""
        trace_file = create_trace_file(
            [
                {
                    "ts": "2025-01-15T10:30:00.123Z",
                    "event": "spec_registered",
                    "spec": "parent@v1",
                    "key": "parent@v1",
                    "has_dependencies": True,
                },
                {
                    "ts": "2025-01-15T10:30:00.456Z",
                    "event": "dependency_added",
                    "parent": "parent@v1",
                    "dependency": "child@v2",
                    "needed_by": "fetch",
                    "needed_by_num": 2,
                },
                {
                    "ts": "2025-01-15T10:30:01.789Z",
                    "event": "cache_hit",
                    "spec": "child@v2",
                    "cache_key": "key123",
                    "pkg_path": "/cache/path",
                },
            ]
        )
        parser = TraceParser(trace_file)

        events = parser.parse()
        self.assertEqual(len(events), 3)
        self.assertEqual(events[0].event, "spec_registered")
        self.assertEqual(events[1].event, "dependency_added")
        self.assertEqual(events[2].event, "cache_hit")

        trace_file.unlink()

    def test_filter_by_event(self):
        """Test filtering events by type."""
        trace_file = create_trace_file(
            [
                {
                    "ts": "2025-01-15T10:30:00.123Z",
                    "event": "phase_start",
                    "spec": "r1",
                    "phase": "recipe_fetch",
                    "phase_num": 0,
                },
                {
                    "ts": "2025-01-15T10:30:00.456Z",
                    "event": "phase_complete",
                    "spec": "r1",
                    "phase": "recipe_fetch",
                    "phase_num": 0,
                    "duration_ms": 100,
                },
                {
                    "ts": "2025-01-15T10:30:00.789Z",
                    "event": "phase_start",
                    "spec": "r1",
                    "phase": "check",
                    "phase_num": 1,
                },
            ]
        )
        parser = TraceParser(trace_file)

        starts = parser.filter_by_event("phase_start")
        self.assertEqual(len(starts), 2)
        self.assertTrue(all(e.event == "phase_start" for e in starts))

        completes = parser.filter_by_event("phase_complete")
        self.assertEqual(len(completes), 1)
        self.assertEqual(completes[0].event, "phase_complete")

        trace_file.unlink()

    def test_filter_by_spec(self):
        """Test filtering events by spec."""
        trace_file = create_trace_file(
            [
                {
                    "ts": "2025-01-15T10:30:00.123Z",
                    "event": "phase_start",
                    "spec": "recipe1@v1",
                    "phase": "recipe_fetch",
                    "phase_num": 0,
                },
                {
                    "ts": "2025-01-15T10:30:00.456Z",
                    "event": "phase_start",
                    "spec": "recipe2@v1",
                    "phase": "recipe_fetch",
                    "phase_num": 0,
                },
                {
                    "ts": "2025-01-15T10:30:00.789Z",
                    "event": "phase_complete",
                    "spec": "recipe1@v1",
                    "phase": "recipe_fetch",
                    "phase_num": 0,
                    "duration_ms": 100,
                },
            ]
        )
        parser = TraceParser(trace_file)

        recipe1_events = parser.filter_by_spec("recipe1@v1")
        self.assertEqual(len(recipe1_events), 2)
        self.assertTrue(all(e.spec == "recipe1@v1" for e in recipe1_events))

        recipe2_events = parser.filter_by_spec("recipe2@v1")
        self.assertEqual(len(recipe2_events), 1)
        self.assertEqual(recipe2_events[0].spec, "recipe2@v1")

        trace_file.unlink()

    def test_get_phase_sequence(self):
        """Test extracting phase execution sequence."""
        trace_file = create_trace_file(
            [
                {
                    "ts": "2025-01-15T10:30:00.100Z",
                    "event": "phase_start",
                    "spec": "test@v1",
                    "phase": "recipe_fetch",
                    "phase_num": 0,
                },
                {
                    "ts": "2025-01-15T10:30:00.200Z",
                    "event": "phase_start",
                    "spec": "test@v1",
                    "phase": "check",
                    "phase_num": 1,
                },
                {
                    "ts": "2025-01-15T10:30:00.300Z",
                    "event": "phase_start",
                    "spec": "test@v1",
                    "phase": "fetch",
                    "phase_num": 2,
                },
            ]
        )
        parser = TraceParser(trace_file)

        sequence = parser.get_phase_sequence("test@v1")
        self.assertEqual(
            sequence,
            [
                PkgPhase.SPEC_FETCH,
                PkgPhase.ASSET_CHECK,
                PkgPhase.ASSET_FETCH,
            ],
        )

        trace_file.unlink()

    def test_assert_dependency_needed_by_success(self):
        """Test asserting dependency needed_by phase (success case)."""
        trace_file = create_trace_file(
            [
                {
                    "ts": "2025-01-15T10:30:00.123Z",
                    "event": "dependency_added",
                    "parent": "parent@v1",
                    "dependency": "child@v1",
                    "needed_by": "fetch",
                    "needed_by_num": 2,
                }
            ]
        )
        parser = TraceParser(trace_file)

        # Should not raise
        parser.assert_dependency_needed_by(
            "parent@v1", "child@v1", PkgPhase.ASSET_FETCH
        )

        trace_file.unlink()

    def test_assert_dependency_needed_by_failure(self):
        """Test asserting dependency needed_by phase (failure case)."""
        trace_file = create_trace_file(
            [
                {
                    "ts": "2025-01-15T10:30:00.123Z",
                    "event": "dependency_added",
                    "parent": "parent@v1",
                    "dependency": "child@v1",
                    "needed_by": "fetch",
                    "needed_by_num": 2,
                }
            ]
        )
        parser = TraceParser(trace_file)

        # Should raise AssertionError
        with self.assertRaises(AssertionError) as cm:
            parser.assert_dependency_needed_by(
                "parent@v1", "child@v1", PkgPhase.ASSET_BUILD
            )

        self.assertIn("Wrong needed_by phase", str(cm.exception))

        trace_file.unlink()

    def test_invalid_json_raises_error(self):
        """Test that invalid JSON raises appropriate error."""
        tmpfile = tempfile.NamedTemporaryFile(mode="w", suffix=".jsonl", delete=False)
        tmpfile.write("not valid json\n")
        tmpfile.close()
        trace_file = Path(tmpfile.name)

        parser = TraceParser(trace_file)

        with self.assertRaises(ValueError) as cm:
            parser.parse()

        self.assertIn("Invalid JSON on line 1", str(cm.exception))

        trace_file.unlink()

    def test_missing_required_fields_raises_error(self):
        """Test that events missing required fields raise error."""
        tmpfile = tempfile.NamedTemporaryFile(mode="w", suffix=".jsonl", delete=False)
        json.dump({"event": "phase_start"}, tmpfile)  # Missing 'ts'
        tmpfile.write("\n")
        tmpfile.close()
        trace_file = Path(tmpfile.name)

        parser = TraceParser(trace_file)

        with self.assertRaises(ValueError) as cm:
            parser.parse()

        self.assertIn("Missing required fields", str(cm.exception))

        trace_file.unlink()


if __name__ == "__main__":
    unittest.main()

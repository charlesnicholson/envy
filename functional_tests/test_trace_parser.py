"""Unit tests for trace_parser module (schema v2)."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from typing import Any

from .trace_parser import SCHEMA_VERSION, PkgPhase, TraceParser

HEADER = {
    "seq": 0,
    "ts": "2025-01-15T10:30:00.000Z",
    "tid": 0,
    "event": "trace_start",
    "schema": SCHEMA_VERSION,
}


def make_event(seq: int, event: str, spec: str | None = None, **fields) -> dict:
    """Build a schema-v2 trace record with envelope keys."""
    record: dict[str, Any] = {
        "seq": seq,
        "ts": "2025-01-15T10:30:00.123Z",
        "tid": 1,
        "event": event,
    }
    if spec is not None:
        record["spec"] = spec
    record.update(fields)
    return record


def create_trace_file(events: list[dict[str, Any]], header: bool = True) -> Path:
    """Helper to create a temporary trace file with given events."""
    tmpfile = tempfile.NamedTemporaryFile(mode="w", suffix=".jsonl", delete=False)
    if header:
        json.dump(HEADER, tmpfile)
        tmpfile.write("\n")
    for event in events:
        json.dump(event, tmpfile)
        tmpfile.write("\n")
    tmpfile.close()
    return Path(tmpfile.name)


class TestTraceParser(unittest.TestCase):
    """Tests for trace_parser module."""

    def test_parse_header_only(self):
        """A trace with only the header record parses to just that record."""
        trace_file = create_trace_file([])
        parser = TraceParser(trace_file)

        events = parser.parse()
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0].event, "trace_start")

        trace_file.unlink()

    def test_missing_header_raises(self):
        """A trace without a trace_start header is rejected."""
        trace_file = create_trace_file(
            [make_event(1, "phase_start", spec="test@v1", phase="spec_fetch")],
            header=False,
        )
        parser = TraceParser(trace_file)

        with self.assertRaises(ValueError) as cm:
            parser.parse()
        self.assertIn("trace_start", str(cm.exception))

        trace_file.unlink()

    def test_schema_mismatch_raises(self):
        """A header with the wrong schema version is rejected."""
        bad_header = dict(HEADER, schema=SCHEMA_VERSION + 1)
        trace_file = create_trace_file([bad_header], header=False)
        parser = TraceParser(trace_file)

        with self.assertRaises(ValueError) as cm:
            parser.parse()
        self.assertIn("schema mismatch", str(cm.exception))

        trace_file.unlink()

    def test_parse_single_event(self):
        """Test parsing a single trace event."""
        trace_file = create_trace_file(
            [make_event(1, "phase_start", spec="test@v1", phase="spec_fetch")]
        )
        parser = TraceParser(trace_file)

        events = parser.parse()
        self.assertEqual(len(events), 2)

        event = events[1]
        self.assertEqual(event.event, "phase_start")
        self.assertEqual(event.ts, "2025-01-15T10:30:00.123Z")
        self.assertEqual(event.seq, 1)
        self.assertEqual(event.tid, 1)
        self.assertEqual(event.spec, "test@v1")
        self.assertEqual(event.phase, PkgPhase.SPEC_FETCH)

        trace_file.unlink()

    def test_events_sorted_by_seq(self):
        """Events are returned in seq order regardless of file order."""
        trace_file = create_trace_file(
            [
                make_event(3, "phase_start", spec="r1", phase="check"),
                make_event(1, "spec_registered", spec="r1", key="r1"),
                make_event(2, "phase_start", spec="r1", phase="spec_fetch"),
            ]
        )
        parser = TraceParser(trace_file)

        events = parser.parse()
        self.assertEqual([e.seq for e in events], [0, 1, 2, 3])
        self.assertEqual(events[1].event, "spec_registered")

        trace_file.unlink()

    def test_dependency_added_spec_is_parent(self):
        """dependency_added carries the parent as its envelope spec."""
        trace_file = create_trace_file(
            [
                make_event(
                    1,
                    "dependency_added",
                    spec="parent@v1",
                    dependency="child@v2",
                    needed_by="fetch",
                )
            ]
        )
        parser = TraceParser(trace_file)

        deps = parser.get_dependency_added_events("parent@v1")
        self.assertEqual(len(deps), 1)
        self.assertEqual(deps[0].spec, "parent@v1")
        self.assertEqual(deps[0].raw["dependency"], "child@v2")

        trace_file.unlink()

    def test_filter_by_event(self):
        """Test filtering events by type."""
        trace_file = create_trace_file(
            [
                make_event(1, "phase_start", spec="r1", phase="spec_fetch"),
                make_event(
                    2, "phase_complete", spec="r1", phase="spec_fetch", duration_ms=100
                ),
                make_event(3, "phase_start", spec="r1", phase="check"),
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

    def test_filter_by_unknown_event_raises(self):
        """Filtering on a name absent from the registry raises (no vacuous [])."""
        trace_file = create_trace_file([])
        parser = TraceParser(trace_file)

        with self.assertRaises(ValueError) as cm:
            parser.filter_by_event("no_such_event")
        self.assertIn("Unknown event type", str(cm.exception))

        trace_file.unlink()

    def test_unknown_event_in_file_raises(self):
        """A record with an unregistered event name is rejected at parse time."""
        trace_file = create_trace_file([make_event(1, "mystery_event", spec="r1")])
        parser = TraceParser(trace_file)

        with self.assertRaises(ValueError) as cm:
            parser.parse()
        self.assertIn("Unknown event type", str(cm.exception))

        trace_file.unlink()

    def test_filter_by_spec(self):
        """Test filtering events by spec."""
        trace_file = create_trace_file(
            [
                make_event(1, "phase_start", spec="recipe1@v1", phase="spec_fetch"),
                make_event(2, "phase_start", spec="recipe2@v1", phase="spec_fetch"),
                make_event(
                    3,
                    "phase_complete",
                    spec="recipe1@v1",
                    phase="spec_fetch",
                    duration_ms=100,
                ),
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
                make_event(1, "phase_start", spec="test@v1", phase="spec_fetch"),
                make_event(2, "phase_start", spec="test@v1", phase="check"),
                make_event(3, "phase_start", spec="test@v1", phase="fetch"),
            ]
        )
        parser = TraceParser(trace_file)

        sequence = parser.get_phase_sequence("test@v1")
        self.assertEqual(
            sequence,
            [
                PkgPhase.SPEC_FETCH,
                PkgPhase.PKG_CHECK,
                PkgPhase.PKG_FETCH,
            ],
        )

        trace_file.unlink()

    def test_assert_dependency_needed_by_success(self):
        """Test asserting dependency needed_by phase (success case)."""
        trace_file = create_trace_file(
            [
                make_event(
                    1,
                    "dependency_added",
                    spec="parent@v1",
                    dependency="child@v1",
                    needed_by="fetch",
                )
            ]
        )
        parser = TraceParser(trace_file)

        # Should not raise
        parser.assert_dependency_needed_by("parent@v1", "child@v1", PkgPhase.PKG_FETCH)

        trace_file.unlink()

    def test_assert_dependency_needed_by_failure(self):
        """Test asserting dependency needed_by phase (failure case)."""
        trace_file = create_trace_file(
            [
                make_event(
                    1,
                    "dependency_added",
                    spec="parent@v1",
                    dependency="child@v1",
                    needed_by="fetch",
                )
            ]
        )
        parser = TraceParser(trace_file)

        # Should raise AssertionError
        with self.assertRaises(AssertionError) as cm:
            parser.assert_dependency_needed_by(
                "parent@v1", "child@v1", PkgPhase.PKG_BUILD
            )

        self.assertIn("Wrong needed_by phase", str(cm.exception))

        trace_file.unlink()

    def test_assert_ordered(self):
        """assert_ordered compares first-match seq values."""
        trace_file = create_trace_file(
            [
                make_event(1, "cache_miss", spec="r1", cache_key="k"),
                make_event(2, "phase_start", spec="r1", phase="fetch"),
            ]
        )
        parser = TraceParser(trace_file)

        parser.assert_ordered(
            lambda e: e.event == "cache_miss",
            lambda e: e.event == "phase_start" and e.phase == PkgPhase.PKG_FETCH,
        )

        with self.assertRaises(AssertionError):
            parser.assert_ordered(
                lambda e: e.event == "phase_start",
                lambda e: e.event == "cache_miss",
            )

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

        self.assertIn("Invalid trace line 1", str(cm.exception))

        trace_file.unlink()

    def test_missing_envelope_keys_raises_error(self):
        """Test that events missing envelope keys raise error."""
        tmpfile = tempfile.NamedTemporaryFile(mode="w", suffix=".jsonl", delete=False)
        json.dump({"event": "phase_start"}, tmpfile)  # Missing seq/ts/tid
        tmpfile.write("\n")
        tmpfile.close()
        trace_file = Path(tmpfile.name)

        parser = TraceParser(trace_file)

        with self.assertRaises(ValueError) as cm:
            parser.parse()

        self.assertIn("Missing envelope key", str(cm.exception))

        trace_file.unlink()


if __name__ == "__main__":
    unittest.main()

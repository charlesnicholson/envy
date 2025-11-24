"""Trace parser for envy structured logging JSONL files.

Parses JSONL trace files emitted by envy --trace=file:path.jsonl and provides
filtering and analysis helpers for test assertions.
"""

import json
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from typing import List, Literal, Optional


class RecipePhase(IntEnum):
    """Recipe phase enum (matches src/recipe_phase.h)"""

    RECIPE_FETCH = 0
    ASSET_CHECK = 1
    ASSET_FETCH = 2
    ASSET_STAGE = 3
    ASSET_BUILD = 4
    ASSET_INSTALL = 5
    ASSET_DEPLOY = 6
    COMPLETION = 7


# Type aliases for event types
EventType = Literal[
    "phase_blocked",
    "phase_unblocked",
    "dependency_added",
    "phase_start",
    "phase_complete",
    "thread_start",
    "thread_complete",
    "recipe_registered",
    "target_extended",
    "lua_ctx_run_start",
    "lua_ctx_run_complete",
    "lua_ctx_fetch_start",
    "lua_ctx_fetch_complete",
    "lua_ctx_extract_start",
    "lua_ctx_extract_complete",
    "cache_hit",
    "cache_miss",
    "lock_acquired",
    "lock_released",
    "fetch_file_start",
    "fetch_file_complete",
]


@dataclass
class TraceEvent:
    """Parsed trace event with typed fields."""

    event: EventType
    ts: str
    raw: dict  # Full JSON for accessing event-specific fields

    @property
    def recipe(self) -> Optional[str]:
        """Get recipe identity from event (if present)."""
        return self.raw.get("recipe") or self.raw.get("parent")

    @property
    def phase(self) -> Optional[RecipePhase]:
        """Get phase from event (if present), returns enum value."""
        if "phase_num" in self.raw:
            return RecipePhase(self.raw["phase_num"])
        if "blocked_at_phase_num" in self.raw:
            return RecipePhase(self.raw["blocked_at_phase_num"])
        if "needed_by_num" in self.raw:
            return RecipePhase(self.raw["needed_by_num"])
        if "target_phase_num" in self.raw:
            return RecipePhase(self.raw["target_phase_num"])
        return None


class TraceParser:
    """Parser for envy trace JSONL files."""

    def __init__(self, trace_file: Path):
        """Initialize parser with trace file path."""
        self.trace_file = Path(trace_file)
        self._events: Optional[List[TraceEvent]] = None

    def parse(self) -> List[TraceEvent]:
        """Parse all events from trace file."""
        if self._events is not None:
            return self._events

        events = []
        with open(self.trace_file) as f:
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue

                try:
                    data = json.loads(line)
                    if "event" not in data or "ts" not in data:
                        raise ValueError(f"Missing required fields (event, ts)")

                    events.append(
                        TraceEvent(event=data["event"], ts=data["ts"], raw=data)
                    )
                except (json.JSONDecodeError, ValueError) as e:
                    raise ValueError(f"Invalid JSON on line {line_num}: {e}") from e

        self._events = events
        return events

    def filter_by_event(self, event_type: EventType) -> List[TraceEvent]:
        """Filter events by type."""
        return [e for e in self.parse() if e.event == event_type]

    def filter_by_recipe(self, recipe: str) -> List[TraceEvent]:
        """Filter events by recipe identity."""
        return [e for e in self.parse() if e.recipe == recipe]

    def filter_by_recipe_and_event(
        self, recipe: str, event_type: EventType
    ) -> List[TraceEvent]:
        """Filter events by recipe and event type."""
        return [e for e in self.parse() if e.recipe == recipe and e.event == event_type]

    def get_dependency_added_events(self, parent: str) -> List[TraceEvent]:
        """Get all dependency_added events for a given parent recipe."""
        return [
            e
            for e in self.filter_by_event("dependency_added")
            if e.raw.get("parent") == parent
        ]

    def get_phase_sequence(self, recipe: str) -> List[RecipePhase]:
        """Get the sequence of phases executed for a recipe."""
        phase_starts = self.filter_by_recipe_and_event(recipe, "phase_start")
        return [RecipePhase(e.raw["phase_num"]) for e in phase_starts]

    def assert_phase_sequence(self, recipe: str, expected: List[RecipePhase]) -> None:
        """Assert that a recipe executed phases in expected order."""
        actual = self.get_phase_sequence(recipe)
        assert actual == expected, (
            f"Phase sequence mismatch for {recipe}:\n"
            f"  Expected: {[p.name for p in expected]}\n"
            f"  Actual:   {[p.name for p in actual]}"
        )

    def assert_dependency_needed_by(
        self, parent: str, dependency: str, expected_phase: RecipePhase
    ) -> None:
        """Assert that a dependency has the expected needed_by phase."""
        deps = self.get_dependency_added_events(parent)
        matching = [e for e in deps if e.raw.get("dependency") == dependency]

        assert matching, f"No dependency_added event found for {parent} -> {dependency}"

        event = matching[0]
        actual_phase = RecipePhase(event.raw["needed_by_num"])

        assert actual_phase == expected_phase, (
            f"Wrong needed_by phase for {parent} -> {dependency}:\n"
            f"  Expected: {expected_phase.name}\n"
            f"  Actual:   {actual_phase.name}"
        )

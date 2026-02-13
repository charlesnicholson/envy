"""Tests for launcher script root directive discovery.

These tests verify that the shell launcher scripts (envy, envy.bat) correctly
implement root-aware manifest discovery matching the C++ manifest::discover().

Test matrix (15 scenarios) using 3 levels: child -> parent -> grandparent
Notation: F=root false, T=root true, A=absent (defaults to true), -=no manifest

| # | child | parent | grandparent | Expected |
|---|-------|--------|-------------|----------|
| 1 | F     | F      | F           | grandparent (topmost non-root) |
| 2 | F     | F      | T           | grandparent (root found) |
| 3 | F     | F      | A           | grandparent (absent=root) |
| 4 | F     | T      | F           | parent (root found, stops) |
| 5 | F     | T      | T           | parent (root found) |
| 6 | F     | T      | A           | parent (root found) |
| 7 | F     | A      | F           | parent (absent=root, stops) |
| 8 | F     | A      | T           | parent (absent=root) |
| 9 | F     | A      | A           | parent (absent=root) |
| 10| T     | F      | F           | child (root found immediately) |
| 11| T     | T      | T           | child (root found immediately) |
| 12| A     | F      | F           | child (absent=root, stops) |
| 13| F     | F      | -           | parent (no grandparent manifest) |
| 14| F     | -      | F           | grandparent (parent has no manifest) |
| 15| F     | -      | -           | child (only manifest in tree) |
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest

from . import test_config
from pathlib import Path


def _make_manifest(root_value: str | None) -> str:
    """Create minimal manifest content with specified root directive."""
    lines = ['-- @envy bin "tools"']
    if root_value is not None:
        lines.append(f'-- @envy root "{root_value}"')
    lines.append("PACKAGES = {}")
    return "\n".join(lines)


def _get_bash_find_manifest_script() -> str:
    """Return a bash script that implements find_manifest and prints result.

    Takes an optional argument to override the starting directory (used by tests
    to simulate the script living inside a project tree).
    """
    return """#!/usr/bin/env bash
set -euo pipefail

resolve_script_dir() {
    local src="${BASH_SOURCE[0]}"
    while [[ -L "$src" ]]; do
        local dir; dir="$(cd -P "$(dirname "$src")" && pwd -P)"
        src="$(readlink "$src")"
        [[ "$src" != /* ]] && src="$dir/$src"
    done
    cd -P "$(dirname "$src")" && pwd -P
}

find_manifest() {
    local d="${1:-$(resolve_script_dir)}"
    local candidates=()
    while [[ "$d" != / ]]; do
        if [[ -f "$d/envy.lua" ]]; then
            local is_root="true"
            if head -20 "$d/envy.lua" | grep -qE '^--[[:space:]]*@envy[[:space:]]+root[[:space:]]+"false"'; then
                is_root="false"
            fi
            if [[ "$is_root" == "true" ]]; then
                echo "$d/envy.lua" && return
            else
                candidates+=("$d/envy.lua")
            fi
        fi
        d="${d%/*}"; d="${d:-/}"
    done
    # Use bash 3.x compatible syntax for last array element
    [[ ${#candidates[@]} -gt 0 ]] && echo "${candidates[${#candidates[@]}-1]}" && return
    return 1
}

find_manifest "$@"
"""


@unittest.skipIf(sys.platform == "win32", "Bash tests skipped on Windows")
class TestBashLauncherRootDiscovery(unittest.TestCase):
    """Test bash launcher's root-aware manifest discovery."""

    def setUp(self) -> None:
        # Use resolve() to get canonical path (handles /var -> /private/var on macOS)
        self._temp_dir = Path(tempfile.mkdtemp(prefix="envy-launcher-root-")).resolve()
        self._grandparent = self._temp_dir / "grandparent"
        self._parent = self._grandparent / "parent"
        self._child = self._parent / "child"
        self._child.mkdir(parents=True)

        # Create the test script
        self._script = self._temp_dir / "test_find_manifest.sh"
        self._script.write_text(_get_bash_find_manifest_script())
        self._script.chmod(0o755)

    def tearDown(self) -> None:
        if hasattr(self, "_temp_dir") and self._temp_dir.exists():
            shutil.rmtree(self._temp_dir, ignore_errors=True)

    def _write_manifest(self, directory: Path, root_value: str | None) -> None:
        """Write a manifest to the given directory."""
        manifest = directory / "envy.lua"
        manifest.write_text(_make_manifest(root_value))

    def _run_find_manifest(self, start_dir: Path) -> Path | None:
        """Run find_manifest from given directory, return discovered manifest path."""
        result = test_config.run(
            [str(self._script), str(start_dir)],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            return None
        return Path(result.stdout.strip())

    def _assert_manifest_at(
        self, expected_dir: Path, start_dir: Path | None = None
    ) -> None:
        """Assert find_manifest returns manifest in expected directory."""
        if start_dir is None:
            start_dir = self._child
        found = self._run_find_manifest(start_dir)
        self.assertIsNotNone(found, "find_manifest should find a manifest")
        expected = expected_dir / "envy.lua"
        self.assertEqual(
            expected, found, f"Expected manifest at {expected}, got {found}"
        )

    # Scenario 1: F F F -> grandparent (topmost non-root)
    def test_scenario_01_fff_uses_grandparent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, "false")
        self._write_manifest(self._grandparent, "false")
        self._assert_manifest_at(self._grandparent)

    # Scenario 2: F F T -> grandparent (root found)
    def test_scenario_02_fft_uses_grandparent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, "false")
        self._write_manifest(self._grandparent, "true")
        self._assert_manifest_at(self._grandparent)

    # Scenario 3: F F A -> grandparent (absent=root)
    def test_scenario_03_ffa_uses_grandparent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, "false")
        self._write_manifest(self._grandparent, None)  # absent = root
        self._assert_manifest_at(self._grandparent)

    # Scenario 4: F T F -> parent (root found, stops)
    def test_scenario_04_ftf_uses_parent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, "true")
        self._write_manifest(self._grandparent, "false")
        self._assert_manifest_at(self._parent)

    # Scenario 5: F T T -> parent (root found)
    def test_scenario_05_ftt_uses_parent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, "true")
        self._write_manifest(self._grandparent, "true")
        self._assert_manifest_at(self._parent)

    # Scenario 6: F T A -> parent (root found)
    def test_scenario_06_fta_uses_parent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, "true")
        self._write_manifest(self._grandparent, None)
        self._assert_manifest_at(self._parent)

    # Scenario 7: F A F -> parent (absent=root, stops)
    def test_scenario_07_faf_uses_parent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, None)  # absent = root
        self._write_manifest(self._grandparent, "false")
        self._assert_manifest_at(self._parent)

    # Scenario 8: F A T -> parent (absent=root)
    def test_scenario_08_fat_uses_parent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, None)  # absent = root
        self._write_manifest(self._grandparent, "true")
        self._assert_manifest_at(self._parent)

    # Scenario 9: F A A -> parent (absent=root)
    def test_scenario_09_faa_uses_parent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, None)  # absent = root
        self._write_manifest(self._grandparent, None)
        self._assert_manifest_at(self._parent)

    # Scenario 10: T F F -> child (root found immediately)
    def test_scenario_10_tff_uses_child(self) -> None:
        self._write_manifest(self._child, "true")
        self._write_manifest(self._parent, "false")
        self._write_manifest(self._grandparent, "false")
        self._assert_manifest_at(self._child)

    # Scenario 11: T T T -> child (root found immediately)
    def test_scenario_11_ttt_uses_child(self) -> None:
        self._write_manifest(self._child, "true")
        self._write_manifest(self._parent, "true")
        self._write_manifest(self._grandparent, "true")
        self._assert_manifest_at(self._child)

    # Scenario 12: A F F -> child (absent=root, stops)
    def test_scenario_12_aff_uses_child(self) -> None:
        self._write_manifest(self._child, None)  # absent = root
        self._write_manifest(self._parent, "false")
        self._write_manifest(self._grandparent, "false")
        self._assert_manifest_at(self._child)

    # Scenario 13: F F - -> parent (no grandparent manifest)
    def test_scenario_13_ff_dash_uses_parent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, "false")
        # No grandparent manifest
        self._assert_manifest_at(self._parent)

    # Scenario 14: F - F -> grandparent (parent has no manifest)
    def test_scenario_14_f_dash_f_uses_grandparent(self) -> None:
        self._write_manifest(self._child, "false")
        # No parent manifest
        self._write_manifest(self._grandparent, "false")
        self._assert_manifest_at(self._grandparent)

    # Scenario 15: F - - -> child (only manifest in tree)
    def test_scenario_15_f_dash_dash_uses_child(self) -> None:
        self._write_manifest(self._child, "false")
        # No parent or grandparent manifest
        self._assert_manifest_at(self._child)


@unittest.skipUnless(sys.platform == "win32", "Windows-only tests")
class TestBatchLauncherRootDiscovery(unittest.TestCase):
    """Test batch launcher's root-aware manifest discovery (Windows only)."""

    def setUp(self) -> None:
        self._temp_dir = Path(tempfile.mkdtemp(prefix="envy-launcher-root-"))
        self._grandparent = self._temp_dir / "grandparent"
        self._parent = self._grandparent / "parent"
        self._child = self._parent / "child"
        self._child.mkdir(parents=True)

        # Create the test batch script
        self._script = self._temp_dir / "test_find_manifest.bat"
        self._script.write_text(self._get_batch_find_manifest_script())

    def tearDown(self) -> None:
        if hasattr(self, "_temp_dir") and self._temp_dir.exists():
            shutil.rmtree(self._temp_dir, ignore_errors=True)

    def _get_batch_find_manifest_script(self) -> str:
        """Return a batch script that implements find_manifest and prints result.

        Takes an optional argument to override the starting directory.
        """
        return """@echo off
setlocal EnableDelayedExpansion

set "MANIFEST="
set "CANDIDATE="
if "%~1"=="" (
    set "DIR=%~dp0"
    if "!DIR:~-1!"=="\\" set "DIR=!DIR:~0,-1!"
) else (
    set "DIR=%~1"
)
:findloop
if exist "!DIR!\\envy.lua" (
    set "IS_ROOT=true"
    for /f "usebackq tokens=1,2,3,4 delims= " %%a in ("!DIR!\\envy.lua") do (
        if "%%a"=="--" if "%%b"=="@envy" if "%%c"=="root" (
            set "VAL=%%d"
            set "VAL=!VAL:"=!"
            if "!VAL!"=="false" set "IS_ROOT=false"
        )
    )
    if "!IS_ROOT!"=="true" (
        set "MANIFEST=!DIR!\\envy.lua"
        goto :found
    ) else (
        set "CANDIDATE=!DIR!\\envy.lua"
    )
)
for %%I in ("!DIR!\\..") do set "PARENT=%%~fI"
if "!PARENT!"=="!DIR!" (
    if defined CANDIDATE (
        set "MANIFEST=!CANDIDATE!"
        goto :found
    )
    echo ERROR: envy.lua not found >&2
    exit /b 1
)
set "DIR=!PARENT!"
goto :findloop
:found
echo !MANIFEST!
"""

    def _write_manifest(self, directory: Path, root_value: str | None) -> None:
        """Write a manifest to the given directory."""
        manifest = directory / "envy.lua"
        manifest.write_text(_make_manifest(root_value))

    def _run_find_manifest(self, start_dir: Path) -> Path | None:
        """Run find_manifest from given directory, return discovered manifest path."""
        result = test_config.run(
            ["cmd", "/c", str(self._script), str(start_dir)],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            return None
        return Path(result.stdout.strip())

    def _assert_manifest_at(
        self, expected_dir: Path, start_dir: Path | None = None
    ) -> None:
        """Assert find_manifest returns manifest in expected directory."""
        if start_dir is None:
            start_dir = self._child
        found = self._run_find_manifest(start_dir)
        self.assertIsNotNone(found, "find_manifest should find a manifest")
        expected = expected_dir / "envy.lua"
        self.assertEqual(
            expected, found, f"Expected manifest at {expected}, got {found}"
        )

    # All 15 scenarios - same as bash tests
    def test_scenario_01_fff_uses_grandparent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, "false")
        self._write_manifest(self._grandparent, "false")
        self._assert_manifest_at(self._grandparent)

    def test_scenario_02_fft_uses_grandparent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, "false")
        self._write_manifest(self._grandparent, "true")
        self._assert_manifest_at(self._grandparent)

    def test_scenario_03_ffa_uses_grandparent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, "false")
        self._write_manifest(self._grandparent, None)
        self._assert_manifest_at(self._grandparent)

    def test_scenario_04_ftf_uses_parent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, "true")
        self._write_manifest(self._grandparent, "false")
        self._assert_manifest_at(self._parent)

    def test_scenario_05_ftt_uses_parent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, "true")
        self._write_manifest(self._grandparent, "true")
        self._assert_manifest_at(self._parent)

    def test_scenario_06_fta_uses_parent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, "true")
        self._write_manifest(self._grandparent, None)
        self._assert_manifest_at(self._parent)

    def test_scenario_07_faf_uses_parent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, None)
        self._write_manifest(self._grandparent, "false")
        self._assert_manifest_at(self._parent)

    def test_scenario_08_fat_uses_parent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, None)
        self._write_manifest(self._grandparent, "true")
        self._assert_manifest_at(self._parent)

    def test_scenario_09_faa_uses_parent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, None)
        self._write_manifest(self._grandparent, None)
        self._assert_manifest_at(self._parent)

    def test_scenario_10_tff_uses_child(self) -> None:
        self._write_manifest(self._child, "true")
        self._write_manifest(self._parent, "false")
        self._write_manifest(self._grandparent, "false")
        self._assert_manifest_at(self._child)

    def test_scenario_11_ttt_uses_child(self) -> None:
        self._write_manifest(self._child, "true")
        self._write_manifest(self._parent, "true")
        self._write_manifest(self._grandparent, "true")
        self._assert_manifest_at(self._child)

    def test_scenario_12_aff_uses_child(self) -> None:
        self._write_manifest(self._child, None)
        self._write_manifest(self._parent, "false")
        self._write_manifest(self._grandparent, "false")
        self._assert_manifest_at(self._child)

    def test_scenario_13_ff_dash_uses_parent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._parent, "false")
        self._assert_manifest_at(self._parent)

    def test_scenario_14_f_dash_f_uses_grandparent(self) -> None:
        self._write_manifest(self._child, "false")
        self._write_manifest(self._grandparent, "false")
        self._assert_manifest_at(self._grandparent)

    def test_scenario_15_f_dash_dash_uses_child(self) -> None:
        self._write_manifest(self._child, "false")
        self._assert_manifest_at(self._child)


if __name__ == "__main__":
    unittest.main()

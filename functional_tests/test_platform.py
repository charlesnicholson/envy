"""Tests for platform utilities including exe path detection."""

import subprocess
import sys
import unittest
from pathlib import Path

from .test_config import get_envy_executable


def _get_envy_binary() -> Path:
    """Get the main envy binary (not functional tester)."""
    root = Path(__file__).parent.parent / "out" / "build"
    if sys.platform == "win32":
        return root / "envy.exe"
    return root / "envy"


class TestPlatformExePath(unittest.TestCase):
    """Verify get_exe_path returns correct path."""

    def _extract_exe_path(self, output: str) -> str:
        """Extract 'Executable: <path>' from version output."""
        for line in output.splitlines():
            if line.startswith("Executable: "):
                return line[len("Executable: ") :]
        raise ValueError("No 'Executable:' line found in output")

    def test_exe_path_matches_invoked_binary(self):
        """Exe path should match the binary we invoked."""
        envy_binary = _get_envy_binary()
        result = subprocess.run(
            [str(envy_binary), "version"],
            capture_output=True,
            text=True,
            check=True,
        )

        reported_path = Path(self._extract_exe_path(result.stderr))
        expected_path = envy_binary.resolve()

        self.assertEqual(reported_path, expected_path)

    def test_exe_path_matches_functional_tester(self):
        """Exe path works for functional tester binary too."""
        functional_tester = get_envy_executable()
        result = subprocess.run(
            [str(functional_tester), "version"],
            capture_output=True,
            text=True,
            check=True,
        )

        reported_path = Path(self._extract_exe_path(result.stderr))
        expected_path = functional_tester.resolve()

        self.assertEqual(reported_path, expected_path)

    def test_exe_path_is_absolute(self):
        """Reported exe path should be absolute."""
        envy_binary = _get_envy_binary()
        result = subprocess.run(
            [str(envy_binary), "version"],
            capture_output=True,
            text=True,
            check=True,
        )

        reported_path = Path(self._extract_exe_path(result.stderr))
        self.assertTrue(reported_path.is_absolute())

    def test_exe_path_exists(self):
        """Reported exe path should exist."""
        envy_binary = _get_envy_binary()
        result = subprocess.run(
            [str(envy_binary), "version"],
            capture_output=True,
            text=True,
            check=True,
        )

        reported_path = Path(self._extract_exe_path(result.stderr))
        self.assertTrue(reported_path.exists())


if __name__ == "__main__":
    unittest.main()

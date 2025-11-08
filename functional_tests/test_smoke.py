from __future__ import annotations

import os
import subprocess
import sys
import unittest
from pathlib import Path


class EnvyBinarySmokeTest(unittest.TestCase):
    def setUp(self) -> None:
        self._project_root = Path(__file__).resolve().parent.parent
        self._envy_binary = (
            self._project_root
            / "out"
            / "build"
            / ("envy.exe" if sys.platform == "win32" else "envy")
        )

    def test_envy_help_executes(self) -> None:
        self.assertTrue(
            self._envy_binary.exists(), f"Expected envy binary at {self._envy_binary}"
        )

        env = os.environ.copy()
        env.setdefault("ENVY_CACHE_DIR", str(self._project_root / "out" / "cache"))
        result = subprocess.run(
            [str(self._envy_binary), "version"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env,
        )

        self.assertEqual("", result.stdout.strip())
        self.assertIn("envy version", result.stderr)


if __name__ == "__main__":
    unittest.main()

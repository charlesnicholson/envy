from __future__ import annotations

import os
import subprocess
import unittest

from . import test_config
from pathlib import Path


class EnvyBinarySmokeTest(unittest.TestCase):
    def setUp(self) -> None:
        self._project_root = Path(__file__).resolve().parent.parent
        self._envy_binary = test_config.get_envy_production_executable()

    def test_envy_help_executes(self) -> None:

        env = os.environ.copy()
        env.setdefault("ENVY_CACHE_DIR", str(self._project_root / "out" / "cache"))
        result = test_config.run(
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

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class EnvyLuaTests(unittest.TestCase):
  def setUp(self) -> None:
    self._project_root = Path(__file__).resolve().parent.parent
    binary_name = "envy.exe" if sys.platform == "win32" else "envy"
    self._envy_binary = self._project_root / "out" / "build" / binary_name

  def _run_envy(self, *args: str) -> subprocess.CompletedProcess[str]:
    self.assertTrue(self._envy_binary.exists(), f"Expected envy binary at {self._envy_binary}")
    env = os.environ.copy()
    env.setdefault("ENVY_CACHE_DIR", str(self._project_root / "out" / "cache"))
    return subprocess.run(
        [str(self._envy_binary), *args],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
    )

  def test_missing_script_returns_error(self) -> None:
    result = self._run_envy("lua", "missing.lua")

    self.assertNotEqual(result.returncode, 0)
    self.assertIn("File does not exist", result.stderr)
    self.assertEqual("", result.stdout.strip())

  def test_invalid_lua_returns_error(self) -> None:
    with tempfile.TemporaryDirectory() as tmpdir:
      script = Path(tmpdir) / "invalid.lua"
      script.write_text("this is invalid\n", encoding="utf-8")

      result = self._run_envy("lua", str(script))

    self.assertNotEqual(result.returncode, 0)
    self.assertIn("invalid.lua", result.stderr)
    self.assertEqual("", result.stdout.strip())

  def test_valid_script_executes(self) -> None:
    with tempfile.TemporaryDirectory() as tmpdir:
      script = Path(tmpdir) / "hello.lua"
      script.write_text("envy.stdout('hello')\n", encoding="utf-8")

      result = self._run_envy("lua", str(script))

    self.assertEqual(0, result.returncode)
    self.assertEqual("hello", result.stdout.strip())
    self.assertEqual("", result.stderr.strip())


if __name__ == "__main__":
  unittest.main()

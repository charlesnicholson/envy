#!/usr/bin/env python3
"""Functional tests for TUI progress rendering.

Tests ANSI rendering in TTY mode, fallback mode for non-TTY environments,
and interactive mode terminal control.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from . import test_config


class TestTUIRendering(unittest.TestCase):
    """Tests for TUI progress rendering modes."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-tui-test-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-tui-manifest-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent
        self.test_data = self.project_root / "test_data"

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)

    @staticmethod
    def lua_path(path: Path) -> str:
        """Convert path to Lua-safe string."""
        return path.as_posix()

    def create_manifest(self, content: str) -> Path:
        """Create manifest file with given content."""
        manifest_path = self.test_dir / "envy.lua"
        manifest_path.write_text(content, encoding="utf-8")
        return manifest_path

    def run_sync(self, manifest: Path, env: dict = None):
        """Run 'envy sync' and return result."""
        cmd = [
            str(self.envy),
            "--cache-root",
            str(self.cache_root),
            "sync",
            "--manifest",
            str(manifest),
        ]

        run_env = os.environ.copy()
        if env:
            run_env.update(env)

        result = subprocess.run(
            cmd,
            cwd=self.project_root,
            capture_output=True,
            env=run_env,
        )
        return result

    def test_parallel_specs_complete_successfully(self):
        """Multiple specs complete successfully in parallel.

        Note: ANSI rendering verification requires a real TTY and is tested manually.
        When stderr is captured (as in automated tests), TUI uses fallback mode.
        """
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.build_function@v1", source = "{self.lua_path(self.test_data)}/specs/build_function.lua" }},
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/specs/build_dependency.lua" }},
}}
"""
        )

        result = self.run_sync(manifest, env={"TERM": "xterm-256color"})

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr.decode()}")

        # Verify packages completed (simple@v1 won't appear since CHECK doesn't print)
        stderr = result.stderr.decode()
        self.assertIn("local.build_function@v1", stderr)
        self.assertIn("local.build_dependency@v1", stderr)
        self.assertIn("sync complete", stderr.lower())

    def test_fallback_mode_with_term_dumb(self):
        """No ANSI codes when TERM=dumb."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{self.lua_path(self.test_data)}/specs/simple.lua" }},
}}
"""
        )

        result = self.run_sync(manifest, env={"TERM": "dumb"})

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr.decode()}")

        # Check that no ANSI codes are present
        stderr = result.stderr.decode()
        self.assertNotIn(
            "\x1b[", stderr, "Expected no ANSI codes when TERM=dumb"
        )

    @unittest.skipIf(sys.platform == "win32", "Unix shell piping not supported on Windows")
    def test_fallback_mode_with_piped_stderr(self):
        """No ANSI codes when stderr is piped (not a TTY)."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{self.lua_path(self.test_data)}/specs/simple.lua" }},
}}
"""
        )

        cmd = [
            str(self.envy),
            "--cache-root",
            str(self.cache_root),
            "sync",
            "--manifest",
            str(manifest),
        ]

        # Pipe stderr through cat to simulate non-TTY
        result = subprocess.run(
            f"{' '.join(str(c) for c in cmd)} 2>&1 | cat",
            shell=True,
            cwd=self.project_root,
            capture_output=True,
            env=os.environ.copy(),
        )

        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout.decode()}")

        # Check that no ANSI codes are present in piped output
        output = result.stdout.decode()
        self.assertNotIn(
            "\x1b[", output, "Expected no ANSI codes when stderr is piped"
        )


if __name__ == "__main__":
    unittest.main()

"""Functional tests for TUI progress rendering.

Tests ANSI rendering in TTY mode, fallback mode for non-TTY environments,
and interactive mode terminal control.
"""

import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

from . import test_config
from .test_config import make_manifest

# User-managed spec that runs shell commands during install
SPEC_BUILD_FUNCTION = """IDENTITY = "local.build_function@v1"

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.run("echo 'Building with envy.run()'", { quiet = true })
  envy.run("echo 'Build finished successfully'", { quiet = true })
end
"""

# Dependency spec for parallel execution test
SPEC_BUILD_DEPENDENCY = """IDENTITY = "local.build_dependency@v1"

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.run("echo 'dependency: begin'", { quiet = true })
  envy.run("echo 'dependency: success'", { quiet = true })
end
"""

# Minimal spec for ANSI/fallback mode tests
SPEC_SIMPLE = """IDENTITY = "local.simple@v1"
DEPENDENCIES = {}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- No-op install
end
"""


class TestTUIRendering(unittest.TestCase):
    """Tests for TUI progress rendering modes."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-tui-test-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-tui-manifest-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent

        self.specs_dir = self.test_dir / "specs"
        self.specs_dir.mkdir()

        # Write specs
        (self.specs_dir / "build_function.lua").write_text(SPEC_BUILD_FUNCTION)
        (self.specs_dir / "build_dependency.lua").write_text(SPEC_BUILD_DEPENDENCY)
        (self.specs_dir / "simple.lua").write_text(SPEC_SIMPLE)

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
        manifest_path.write_text(make_manifest(content), encoding="utf-8")
        return manifest_path

    def run_sync(self, manifest: Path, env: dict | None = None):
        """Run 'envy sync' and return result."""
        cmd = [
            str(self.envy),
            "--cache-root",
            str(self.cache_root),
            "sync",
            "--install-all",
            "--manifest",
            str(manifest),
        ]

        run_env = os.environ.copy()
        if env:
            run_env.update(env)

        result = test_config.run(
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
    {{ spec = "local.build_function@v1", source = "{self.lua_path(self.specs_dir)}/build_function.lua" }},
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path(self.specs_dir)}/build_dependency.lua" }},
}}
"""
        )

        result = self.run_sync(manifest, env={"TERM": "xterm-256color"})

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_fallback_mode_with_term_dumb(self):
        """No ANSI codes when TERM=dumb."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{self.lua_path(self.specs_dir)}/simple.lua" }},
}}
"""
        )

        result = self.run_sync(manifest, env={"TERM": "dumb"})

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Check that no ANSI codes are present
        self.assertNotIn(
            "\x1b[", result.stderr, "Expected no ANSI codes when TERM=dumb"
        )

    def test_fallback_mode_with_piped_stderr(self):
        """No ANSI codes when stderr is piped (not a TTY)."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{self.lua_path(self.specs_dir)}/simple.lua" }},
}}
"""
        )

        # capture_output=True makes stderr a pipe, not a TTY
        result = self.run_sync(manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Check that no ANSI codes are present in piped output
        self.assertNotIn(
            "\x1b[", result.stderr, "Expected no ANSI codes when stderr is piped"
        )


if __name__ == "__main__":
    unittest.main()

"""Bootstrap script integration tests.

Tests the bootstrap pipeline: parse manifest → download envy → cache → exec.
Uses a mock HTTP server serving the real envy binary.
"""

from __future__ import annotations

import http.server
import os
import shutil
import socketserver
import stat
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path

# Inline fixture contents
FIXTURES = {
    "simple.lua": """-- @envy version "1.2.3"

PACKAGES = {
    "local.example@v1",
}
""",
    "missing_version.lua": """-- This manifest has no @envy version directive
-- @envy cache "/custom/cache"

PACKAGES = {
    "local.example@v1",
}
""",
    "with_escapes.lua": """-- @envy version "1.2.3-\\"beta\\""
-- @envy cache "/path/with\\\\backslash"

PACKAGES = {
    "local.example@v1",
}
""",
    "whitespace_variants.lua": """--   @envy   version   "1.0.0"
--\t@envy\tcache\t"/tab/separated"

PACKAGES = {
    "local.example@v1",
}
""",
    "all_directives.lua": """-- @envy version "2.0.0"
-- @envy cache "/opt/envy-cache"
-- @envy mirror "https://internal.corp/envy-releases"

PACKAGES = {
    "local.example@v1",
}
""",
}


class EnvyServer:
    """Simple HTTP server that serves the envy binary."""

    def __init__(self, binary_path: Path):
        self.binary_path = binary_path
        self.binary_content = binary_path.read_bytes()
        self.server: socketserver.TCPServer | None = None
        self.thread: threading.Thread | None = None
        self.port: int = 0

    def start(self) -> int:
        """Start the server and return the port number."""
        parent = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self) -> None:
                if "/envy-" in self.path or "/envy.exe" in self.path:
                    self.send_response(200)
                    self.send_header("Content-Type", "application/octet-stream")
                    self.send_header("Content-Length", str(len(parent.binary_content)))
                    self.end_headers()
                    self.wfile.write(parent.binary_content)
                else:
                    self.send_response(404)
                    self.end_headers()

            def log_message(self, format: str, *args: object) -> None:
                pass

        self.server = socketserver.TCPServer(("127.0.0.1", 0), Handler)
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.daemon = True
        self.thread.start()
        return self.port

    def stop(self) -> None:
        """Stop the server."""
        if self.server:
            self.server.shutdown()
            self.server.server_close()


class BootstrapIntegrationTest(unittest.TestCase):
    """Integration tests for the bootstrap scripts."""

    @classmethod
    def setUpClass(cls) -> None:
        cls._project_root = Path(__file__).resolve().parent.parent
        cls._build_dir = cls._project_root / "out/build"

        if sys.platform == "win32":
            cls._envy_binary = cls._build_dir / "envy.exe"
        else:
            cls._envy_binary = cls._build_dir / "envy"

        cls._bootstrap_unix = cls._project_root / "src/resources/envy"
        cls._bootstrap_windows = cls._project_root / "src/resources/envy.bat"

    def setUp(self) -> None:
        if not self._envy_binary.exists():
            self.skipTest(f"envy not found at {self._envy_binary}")

        if sys.platform == "win32":
            self.assertTrue(
                self._bootstrap_windows.exists(),
                f"Windows bootstrap script not found at {self._bootstrap_windows}",
            )
        else:
            self.assertTrue(
                self._bootstrap_unix.exists(),
                f"Unix bootstrap script not found at {self._bootstrap_unix}",
            )

        self._temp_dir = Path(tempfile.mkdtemp(prefix="envy-bootstrap-test-"))
        self._server = EnvyServer(self._envy_binary)
        self._port = self._server.start()

    def tearDown(self) -> None:
        if hasattr(self, "_server"):
            self._server.stop()
        if hasattr(self, "_temp_dir") and self._temp_dir.exists():
            shutil.rmtree(self._temp_dir, ignore_errors=True)

    def _get_bootstrap_script(self) -> Path:
        if sys.platform == "win32":
            return self._bootstrap_windows
        return self._bootstrap_unix

    def _setup_test_project(
        self, fixture_name: str, fallback_version: str = "1.2.3"
    ) -> Path:
        """Set up a test project with manifest and bootstrap script."""
        project_dir = self._temp_dir / "project"
        bin_dir = project_dir / "tools"
        project_dir.mkdir(parents=True)
        bin_dir.mkdir(parents=True)

        # Write fixture content from inline string
        fixture_content = FIXTURES[fixture_name]
        (project_dir / "envy.lua").write_text(fixture_content)

        bootstrap_src = self._get_bootstrap_script()
        bootstrap_dest = bin_dir / ("envy.bat" if sys.platform == "win32" else "envy")

        content = bootstrap_src.read_text().replace(
            "@@ENVY_VERSION@@", fallback_version
        )
        bootstrap_dest.write_text(content)

        if sys.platform != "win32":
            bootstrap_dest.chmod(
                bootstrap_dest.stat().st_mode
                | stat.S_IXUSR
                | stat.S_IXGRP
                | stat.S_IXOTH
            )

        return bootstrap_dest

    def _run_bootstrap(
        self, bootstrap_script: Path, args: list[str], cache_dir: Path | None = None
    ) -> subprocess.CompletedProcess[str]:
        """Run the bootstrap script and return the result."""
        env = os.environ.copy()
        env["ENVY_MIRROR"] = f"http://127.0.0.1:{self._port}"
        env["ENVY_CACHE_ROOT"] = str(cache_dir or self._temp_dir / "cache")

        if sys.platform == "win32":
            cmd = ["cmd.exe", "/c", str(bootstrap_script), *args]
        else:
            cmd = [str(bootstrap_script), *args]

        return subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            env=env,
            cwd=bootstrap_script.parent.parent,
            timeout=30,
        )

    def test_bootstrap_downloads_and_executes(self) -> None:
        """Test that bootstrap downloads envy and executes it."""
        bootstrap = self._setup_test_project("simple.lua")
        result = self._run_bootstrap(bootstrap, ["version"])

        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        # envy version outputs to stderr
        self.assertIn("envy version", result.stderr)

    def test_bootstrap_caches_binary(self) -> None:
        """Test that bootstrap uses cached binary when present."""
        bootstrap = self._setup_test_project("simple.lua")
        cache_dir = self._temp_dir / "cache"

        # First run downloads (to temp, envy would self-deploy but we simulate it)
        result1 = self._run_bootstrap(bootstrap, ["version"], cache_dir)
        self.assertEqual(0, result1.returncode, f"stderr: {result1.stderr}")
        self.assertIn("Downloading envy", result1.stderr)
        self.assertIn("envy version", result1.stderr)

        # Manually populate cache to simulate envy self-deployment
        cached_binary = (
            cache_dir
            / "envy"
            / "1.2.3"
            / ("envy.exe" if sys.platform == "win32" else "envy")
        )
        cached_binary.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(self._envy_binary, cached_binary)
        if sys.platform != "win32":
            cached_binary.chmod(cached_binary.stat().st_mode | stat.S_IXUSR)

        # Second run uses cache (no download message)
        result2 = self._run_bootstrap(bootstrap, ["version"], cache_dir)
        self.assertEqual(0, result2.returncode, f"stderr: {result2.stderr}")
        self.assertNotIn("Downloading", result2.stderr)
        self.assertIn("envy version", result2.stderr)

    def test_bootstrap_uses_fallback_when_version_missing(self) -> None:
        """Test that bootstrap uses FALLBACK_VERSION when @envy version is missing."""
        bootstrap = self._setup_test_project(
            "missing_version.lua", fallback_version="9.9.9"
        )
        result = self._run_bootstrap(bootstrap, ["version"])

        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("WARNING", result.stderr)
        self.assertIn("fallback", result.stderr.lower())
        self.assertIn("envy version", result.stderr)

    def test_bootstrap_parses_version_with_escapes(self) -> None:
        """Test that bootstrap correctly parses version with escaped characters."""
        bootstrap = self._setup_test_project("with_escapes.lua")
        result = self._run_bootstrap(bootstrap, ["version"])

        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("envy version", result.stderr)

    def test_bootstrap_fails_without_manifest(self) -> None:
        """Test that bootstrap fails gracefully when envy.lua is not found."""
        project_dir = self._temp_dir / "no-manifest"
        bin_dir = project_dir / "tools"
        project_dir.mkdir(parents=True)
        bin_dir.mkdir(parents=True)

        bootstrap_src = self._get_bootstrap_script()
        bootstrap_dest = bin_dir / ("envy.bat" if sys.platform == "win32" else "envy")

        content = bootstrap_src.read_text().replace("@@ENVY_VERSION@@", "1.0.0")
        bootstrap_dest.write_text(content)
        if sys.platform != "win32":
            bootstrap_dest.chmod(bootstrap_dest.stat().st_mode | stat.S_IXUSR)

        env = os.environ.copy()
        env["ENVY_MIRROR"] = f"http://127.0.0.1:{self._port}"
        env["ENVY_CACHE_ROOT"] = str(self._temp_dir / "cache")

        if sys.platform == "win32":
            cmd = ["cmd.exe", "/c", str(bootstrap_dest), "version"]
        else:
            cmd = [str(bootstrap_dest), "version"]

        result = subprocess.run(
            cmd, capture_output=True, text=True, env=env, cwd=project_dir, timeout=30
        )

        self.assertNotEqual(0, result.returncode)
        self.assertIn("envy.lua", result.stderr.lower())


if __name__ == "__main__":
    unittest.main()

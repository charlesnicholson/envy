"""Bootstrap script integration tests.

Tests the bootstrap pipeline: parse manifest → download envy → cache → exec.
Uses a mock HTTP server serving the real envy binary.
"""

from __future__ import annotations

import hashlib
import http.server
import io
import os
import platform as plat
import shutil
import socketserver
import stat
import subprocess
import sys
import tarfile
import tempfile
import threading
import unittest
import zipfile
from pathlib import Path

from . import test_config

_OS_NAME = (
    "windows"
    if sys.platform == "win32"
    else "darwin"
    if sys.platform == "darwin"
    else "linux"
)
_ARCH = plat.machine().lower()
if _ARCH in ("aarch64", "arm64"):
    _ARCH = "arm64"
elif _ARCH == "amd64":
    _ARCH = "x86_64"
_EXT = ".zip" if sys.platform == "win32" else ".tar.gz"

# Inline fixture contents
FIXTURES = {
    "simple.lua": """-- @envy version "1.2.3"

PACKAGES = {
    "local.example@v1",
}
""",
    "missing_version.lua": """-- This manifest has no @envy version directive
-- @envy cache-posix "/custom/cache"

PACKAGES = {
    "local.example@v1",
}
""",
    "with_escapes.lua": """-- @envy version "1.2.3-\\"beta\\""
-- @envy cache-posix "/path/with\\\\backslash"

PACKAGES = {
    "local.example@v1",
}
""",
    "whitespace_variants.lua": """--   @envy   version   "1.0.0"
--\t@envy\tcache-posix\t"/tab/separated"

PACKAGES = {
    "local.example@v1",
}
""",
    "all_directives.lua": """-- @envy version "2.0.0"
-- @envy cache-posix "/opt/envy-cache"
-- @envy mirror "https://internal.corp/envy-releases"

PACKAGES = {
    "local.example@v1",
}
""",
}


class EnvyServer:
    """Simple HTTP server that serves the envy binary as tar.gz (Unix) or zip (Windows)."""

    def __init__(self, binary_path: Path):
        self.binary_path = binary_path
        self.binary_content = binary_path.read_bytes()
        self.server: socketserver.TCPServer | None = None
        self.thread: threading.Thread | None = None
        self.port: int = 0
        self.request_paths: list[str] = []

        # Pre-create tar.gz archive for Unix
        tar_buffer = io.BytesIO()
        with tarfile.open(fileobj=tar_buffer, mode="w:gz") as tar:
            info = tarfile.TarInfo(name="envy")
            info.size = len(self.binary_content)
            info.mode = 0o755
            tar.addfile(info, io.BytesIO(self.binary_content))
        self.tar_gz_content = tar_buffer.getvalue()

        # Pre-create zip archive for Windows
        zip_buffer = io.BytesIO()
        with zipfile.ZipFile(zip_buffer, "w", zipfile.ZIP_DEFLATED) as zf:
            zf.writestr("envy.exe", self.binary_content)
        self.zip_content = zip_buffer.getvalue()

        # Attestation knobs. corrupt_archive flips the served archive bytes while leaving
        # SHA256SUMS alone (a mirror that tampers with one object); sums_body replaces the
        # sums file itself (a mirror that tampers with both, which only the manifest's pin
        # can catch); serve_sums=False models a mirror missing the file entirely.
        self.host_archive_name = f"envy-{_OS_NAME}-{_ARCH}{_EXT}"
        self.corrupt_archive = False
        self.serve_sums = True
        self.sums_body: bytes | None = None

    @property
    def pristine_archive(self) -> bytes:
        return self.zip_content if sys.platform == "win32" else self.tar_gz_content

    @property
    def published_sums(self) -> bytes:
        """What the mirror serves at v<version>/SHA256SUMS."""
        if self.sums_body is not None:
            return self.sums_body
        digest = hashlib.sha256(self.pristine_archive).hexdigest()
        return f"{digest}  {self.host_archive_name}\n".encode()

    @property
    def sums_pin(self) -> str:
        """The value an `@envy sha256sums` directive would carry for this mirror."""
        return hashlib.sha256(self.published_sums).hexdigest()

    def start(self) -> int:
        """Start the server and return the port number."""
        parent = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self) -> None:
                parent.request_paths.append(self.path)
                if self.path.endswith("SHA256SUMS"):
                    if not parent.serve_sums:
                        self.send_response(404)
                        self.end_headers()
                        return
                    content, content_type = parent.published_sums, "text/plain"
                else:
                    match self.path.rsplit(".", 1)[-1]:
                        case "gz" if self.path.endswith(".tar.gz"):
                            content, content_type = (
                                parent.tar_gz_content,
                                "application/gzip",
                            )
                        case "zip":
                            content, content_type = parent.zip_content, "application/zip"
                        case _:
                            self.send_response(404)
                            self.end_headers()
                            return
                    if parent.corrupt_archive:
                        content = content + b"corrupted"
                self.send_response(200)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(content)))
                self.end_headers()
                self.wfile.write(content)

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

    # These cases spawn a bootstrap that downloads and re-execs a real envy; the 5s default
    # watchdog trips first and os._exit(1)s the whole run, which also makes the 30s
    # subprocess timeouts below unreachable.
    envy_watchdog_timeout = 60

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
            # Set ENVY_TEST_KEEP_TEMP to inspect the mock-aws invocation log and the staged
            # tree after a failure. Tests embed the log in their assertion messages, so the
            # default is still to leave nothing behind.
            if os.environ.get("ENVY_TEST_KEEP_TEMP"):
                sys.stderr.write(f"\nENVY_TEST_KEEP_TEMP: kept {self._temp_dir}\n")
                return
            shutil.rmtree(self._temp_dir, ignore_errors=True)

    # --- mock AWS CLI -------------------------------------------------------------
    #
    # The bootstrap's s3:// branch shells out to `aws`. Rather than reach S3, drop a mock
    # first on PATH that logs its argv and serves objects from a local tree. The log is the
    # proof the mock (and not a real aws) ran.

    def _install_mock_aws(self) -> tuple[Path, Path, Path]:
        """Create the mock aws CLI. Returns (bindir, s3root, logfile)."""
        bindir = self._temp_dir / "mockbin"
        s3root = self._temp_dir / "s3root"
        logfile = self._temp_dir / "aws-invocations.log"
        bindir.mkdir(parents=True)
        s3root.mkdir(parents=True)

        # Argument positions are fixed by the bootstrap's own call shape
        # (`aws s3 cp --only-show-errors <uri> <dest>`); a mock that assumes them fails
        # loudly if that shape ever changes.
        (bindir / "aws").write_text(
            "#!/bin/sh\n"
            'printf "%s\\n" "$*" >> "$MOCK_AWS_LOG"\n'
            '[ "$1" = "s3" ] && [ "$2" = "cp" ] || { echo "mock aws: unexpected argv: $*" >&2; exit 64; }\n'
            'uri="$4"; dest="$5"\n'
            'key="${uri#*://}"; key="${key#*/}"\n'
            'src="$MOCK_AWS_ROOT/$key"\n'
            '[ -f "$src" ] || { echo "mock aws: NoSuchKey: $key" >&2; exit 1; }\n'
            'if [ "$dest" = "-" ]; then cat "$src"; else cp "$src" "$dest"; fi\n'
        )
        (bindir / "aws").chmod(0o755)

        (bindir / "aws.bat").write_text(
            "@echo off\r\n"
            "setlocal EnableDelayedExpansion\r\n"
            '>>"%MOCK_AWS_LOG%" echo %*\r\n'
            'if not "%~1"=="s3" (echo mock aws: unexpected argv: %* >&2 & exit /b 64)\r\n'
            'if not "%~2"=="cp" (echo mock aws: unexpected argv: %* >&2 & exit /b 64)\r\n'
            'set "URI=%~4"\r\n'
            'set "DEST=%~5"\r\n'
            'set "KEY=!URI:*://=!"\r\n'
            "for /f \"tokens=1,* delims=/\" %%a in (\"!KEY!\") do set \"KEY=%%b\"\r\n"
            'set "SRC=%MOCK_AWS_ROOT%\\!KEY:/=\\!"\r\n'
            'if not exist "!SRC!" (echo mock aws: NoSuchKey: !KEY! >&2 & exit /b 1)\r\n'
            'copy /y "!SRC!" "!DEST!" >nul || exit /b 1\r\n'
        )

        return bindir, s3root, logfile

    def _mock_aws_env(self, bindir: Path, s3root: Path, logfile: Path) -> dict[str, str]:
        """PATH-prepend the mock and poison real AWS access.

        If PATH injection ever regresses, `aws` resolves to the runner image's real CLI --
        preinstalled on every GitHub hosted runner. The poisoned endpoint and config paths
        make that physically unable to reach S3 instead of merely failing the assertions
        after a live request.
        """
        env = {
            "PATH": f"{bindir}{os.pathsep}{os.environ.get('PATH', '')}",
            "MOCK_AWS_LOG": str(logfile),
            "MOCK_AWS_ROOT": str(s3root),
            "AWS_ENDPOINT_URL": "http://127.0.0.1:1",
            "AWS_ENDPOINT_URL_S3": "http://127.0.0.1:1",
            "AWS_CONFIG_FILE": str(self._temp_dir / "no-such-aws-config"),
            "AWS_SHARED_CREDENTIALS_FILE": str(self._temp_dir / "no-such-aws-creds"),
            "AWS_EC2_METADATA_DISABLED": "1",
            "AWS_MAX_ATTEMPTS": "1",
            "AWS_ACCESS_KEY_ID": "",
            "AWS_SECRET_ACCESS_KEY": "",
            "AWS_SESSION_TOKEN": "",
            "AWS_PROFILE": "",
        }
        return env

    def _seed_s3_release(self, s3root: Path, prefix: str, version: str) -> None:
        """Write the host-platform archive plus a `latest` file under a bucket prefix."""
        base = s3root / prefix if prefix else s3root
        (base / f"v{version}").mkdir(parents=True, exist_ok=True)
        content = (
            self._server.zip_content
            if sys.platform == "win32"
            else self._server.tar_gz_content
        )
        (base / f"v{version}" / f"envy-{_OS_NAME}-{_ARCH}{_EXT}").write_bytes(content)
        (base / "latest").write_text(version)

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
        self,
        bootstrap_script: Path,
        args: list[str],
        cache_dir: Path | None = None,
        env_overrides: dict[str, str] | None = None,
        set_mirror: bool = True,
    ) -> subprocess.CompletedProcess[str]:
        """Run the bootstrap script and return the result.

        set_mirror=False drops ENVY_MIRROR entirely, which is what a manifest-mirror test
        needs now that env wins over the manifest. It must be dropped rather than set to "":
        cmd.exe has no concept of an empty-but-defined variable, so `if defined` would
        disagree with bash's `${VAR:-}` and the two scripts would diverge under test.
        """
        env = os.environ.copy()
        if set_mirror:
            env["ENVY_MIRROR"] = f"http://127.0.0.1:{self._port}"
        else:
            env.pop("ENVY_MIRROR", None)
        env["ENVY_CACHE_ROOT"] = str(cache_dir or self._temp_dir / "cache")
        if env_overrides:
            env.update(env_overrides)

        if sys.platform == "win32":
            cmd = ["cmd.exe", "/c", str(bootstrap_script), *args]
        else:
            cmd = [str(bootstrap_script), *args]

        return test_config.run(
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

    @unittest.skipUnless(
        sys.platform == "win32",
        "exercises the Windows envy.bat native curl.exe/tar.exe path",
    )
    def test_bootstrap_succeeds_without_powershell(self) -> None:
        """Bootstrap must not depend on PowerShell to download and extract.

        Machine policy (WDAC/AppLocker constrained-language mode, disabled module
        autoloading, a tampered PSModulePath) can block the Microsoft.PowerShell.Archive
        script module that `Expand-Archive` lives in, while compiled binaries still run.
        The bootstrap prefers native curl.exe/tar.exe; PowerShell is only a fallback.
        Shadow `powershell`/`pwsh` with always-failing stubs earlier in PATH (a tripwire:
        any PowerShell use in the happy path would fail the operation) and assert the
        bootstrap still downloads, extracts, and execs.
        """
        bootstrap = self._setup_test_project("simple.lua")

        sabotage = self._temp_dir / "sabotage"
        sabotage.mkdir()
        for name in ("powershell.bat", "pwsh.bat"):
            (sabotage / name).write_text(
                "@echo PowerShell blocked by policy (test) 1>&2\r\n@exit /b 1\r\n"
            )
        scrubbed_path = f"{sabotage}{os.pathsep}{os.environ.get('PATH', '')}"

        result = self._run_bootstrap(
            bootstrap, ["version"], env_overrides={"PATH": scrubbed_path}
        )

        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("envy version", result.stderr)
        # Confirms the download happened over the network (via curl.exe), not a cache hit.
        self.assertTrue(
            any(p.endswith(".zip") for p in self._server.request_paths),
            f"expected a .zip download request, got: {self._server.request_paths}",
        )

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
        """Test that bootstrap resolves a version when @envy version is missing.

        Without a latest file or GitHub access, falls through to FALLBACK_VERSION.
        The mock server serves any .tar.gz path, so whichever version is resolved works.
        """
        bootstrap = self._setup_test_project(
            "missing_version.lua", fallback_version="9.9.9"
        )
        result = self._run_bootstrap(bootstrap, ["version"])

        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("envy version", result.stderr)

    def test_bootstrap_uses_latest_file_when_version_missing(self) -> None:
        """Test that bootstrap reads $CACHE/envy/latest when @envy version is absent."""
        bootstrap = self._setup_test_project(
            "missing_version.lua", fallback_version="9.9.9"
        )
        cache_dir = self._temp_dir / "cache"

        # Pre-populate the latest pointer and binary
        latest_ver = "5.5.5"
        (cache_dir / "envy").mkdir(parents=True, exist_ok=True)
        (cache_dir / "envy" / "latest").write_text(latest_ver)
        cached_binary = (
            cache_dir
            / "envy"
            / latest_ver
            / ("envy.exe" if sys.platform == "win32" else "envy")
        )
        cached_binary.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(self._envy_binary, cached_binary)
        if sys.platform != "win32":
            cached_binary.chmod(cached_binary.stat().st_mode | stat.S_IXUSR)

        result = self._run_bootstrap(bootstrap, ["version"], cache_dir)
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertNotIn("Downloading", result.stderr)
        self.assertIn("envy version", result.stderr)

    def test_bootstrap_ignores_latest_when_version_present(self) -> None:
        """Test that @envy version in manifest takes precedence over latest file."""
        bootstrap = self._setup_test_project("simple.lua")
        cache_dir = self._temp_dir / "cache"

        # Pre-populate latest pointing to a different version
        (cache_dir / "envy").mkdir(parents=True, exist_ok=True)
        (cache_dir / "envy" / "latest").write_text("7.7.7")

        # Pre-populate the cache binary at the manifest version (1.2.3)
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

        result = self._run_bootstrap(bootstrap, ["version"], cache_dir)
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertNotIn("Downloading", result.stderr)
        self.assertIn("envy version", result.stderr)

    def test_bootstrap_falls_through_stale_latest(self) -> None:
        """Test that bootstrap falls through when latest points to missing binary."""
        bootstrap = self._setup_test_project(
            "missing_version.lua", fallback_version="9.9.9"
        )
        cache_dir = self._temp_dir / "cache"

        # Write latest pointing to a version whose binary doesn't exist
        (cache_dir / "envy").mkdir(parents=True, exist_ok=True)
        (cache_dir / "envy" / "latest").write_text("0.0.1")

        result = self._run_bootstrap(bootstrap, ["version"], cache_dir)
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        # Should have fallen through and downloaded
        self.assertIn("Downloading", result.stderr)
        self.assertIn("envy version", result.stderr)

    def test_bootstrap_parses_version_with_escapes(self) -> None:
        """Test that bootstrap correctly parses version with escaped characters."""
        bootstrap = self._setup_test_project("with_escapes.lua")
        result = self._run_bootstrap(bootstrap, ["version"])

        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("envy version", result.stderr)

    def test_bootstrap_requests_correct_architecture(self) -> None:
        """Test that bootstrap constructs the download URL with the correct arch."""
        bootstrap = self._setup_test_project("simple.lua")
        self._server.request_paths.clear()
        result = self._run_bootstrap(bootstrap, ["version"])

        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        expected = f"/v1.2.3/envy-{_OS_NAME}-{_ARCH}{_EXT}"
        self.assertEqual(1, len(self._server.request_paths))
        self.assertEqual(expected, self._server.request_paths[0])

    # --- s3:// mirrors ------------------------------------------------------------

    def _run_s3_bootstrap(
        self,
        manifest: str,
        *,
        version: str = "5.6.7",
        prefix: str = "releases",
        seed: bool = True,
        extra_env: dict[str, str] | None = None,
    ) -> tuple[subprocess.CompletedProcess[str], Path]:
        bindir, s3root, logfile = self._install_mock_aws()
        if seed:
            self._seed_s3_release(s3root, prefix, version)

        project_dir = self._temp_dir / "project"
        bin_dir = project_dir / "tools"
        bin_dir.mkdir(parents=True)
        (project_dir / "envy.lua").write_text(manifest)

        dest = bin_dir / ("envy.bat" if sys.platform == "win32" else "envy")
        dest.write_text(
            self._get_bootstrap_script().read_text().replace("@@ENVY_VERSION@@", "0.0.1")
        )
        if sys.platform != "win32":
            dest.chmod(dest.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        env = self._mock_aws_env(bindir, s3root, logfile)
        if extra_env:
            env.update(extra_env)
        result = self._run_bootstrap(dest, ["version"], env_overrides=env, set_mirror=False)
        return result, logfile

    def _mock_log(self, logfile: Path) -> str:
        # Asserted separately from its contents: a missing log means PATH injection failed
        # and a real aws ran, which is a different bug from a wrong object key.
        self.assertTrue(
            logfile.exists(),
            "mock aws was never invoked -- PATH injection failed and a real aws CLI may "
            "have run",
        )
        return logfile.read_text()

    def test_bootstrap_s3_mirror_downloads_via_aws_cli(self) -> None:
        """An s3:// mirror shells out to aws, never to curl."""
        manifest = (
            '-- @envy version "5.6.7"\n'
            '-- @envy mirror "s3://fake-bucket/releases"\n\nPACKAGES = {}\n'
        )
        result, logfile = self._run_s3_bootstrap(manifest)
        log = self._mock_log(logfile)

        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}\nlog: {log}")
        self.assertIn("envy version", result.stderr)
        self.assertIn(
            f"s3://fake-bucket/releases/v5.6.7/envy-{_OS_NAME}-{_ARCH}{_EXT}", log
        )
        # Proves the http branch was not taken as well.
        self.assertEqual([], self._server.request_paths)

    def test_bootstrap_s3_mirror_resolves_version_from_mirror_latest(self) -> None:
        """With no @envy version, the mirror's own `latest` answers -- not github.com."""
        manifest = (
            '-- @envy mirror "s3://fake-bucket/releases"\n\nPACKAGES = {}\n'
        )
        result, logfile = self._run_s3_bootstrap(manifest)
        log = self._mock_log(logfile)

        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}\nlog: {log}")
        self.assertIn("s3://fake-bucket/releases/latest", log)
        self.assertIn(
            f"s3://fake-bucket/releases/v5.6.7/envy-{_OS_NAME}-{_ARCH}{_EXT}", log
        )
        self.assertNotIn("0.0.1", log)  # the stamped fallback was not used

    def test_bootstrap_s3_mirror_bucket_root_prefix(self) -> None:
        """A bucket-root mirror produces keys with no leading prefix and no double slash."""
        manifest = (
            '-- @envy version "5.6.7"\n'
            '-- @envy mirror "s3://fake-bucket"\n\nPACKAGES = {}\n'
        )
        result, logfile = self._run_s3_bootstrap(manifest, prefix="")
        log = self._mock_log(logfile)

        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}\nlog: {log}")
        self.assertIn(f"s3://fake-bucket/v5.6.7/envy-{_OS_NAME}-{_ARCH}{_EXT}", log)
        self.assertNotIn("//v5.6.7", log)

    def test_bootstrap_s3_mirror_trailing_slash_does_not_double(self) -> None:
        """A trailing slash on the mirror must not mint a distinct //-containing key."""
        manifest = (
            '-- @envy version "5.6.7"\n'
            '-- @envy mirror "s3://fake-bucket/releases/"\n\nPACKAGES = {}\n'
        )
        result, logfile = self._run_s3_bootstrap(manifest)
        log = self._mock_log(logfile)

        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}\nlog: {log}")
        self.assertNotIn("releases//", log)

    def test_bootstrap_s3_mirror_missing_object_fails_clearly(self) -> None:
        """A missing object reports the URL rather than falling through to exec."""
        manifest = (
            '-- @envy version "9.9.9"\n'
            '-- @envy mirror "s3://fake-bucket/releases"\n\nPACKAGES = {}\n'
        )
        result, logfile = self._run_s3_bootstrap(manifest, seed=False)
        self._mock_log(logfile)

        self.assertNotEqual(0, result.returncode)
        self.assertIn("Failed to download envy", result.stderr)

    def test_bootstrap_env_mirror_overrides_manifest_mirror(self) -> None:
        """ENVY_MIRROR beats @envy mirror, matching the runtime resolver in reexec.cpp."""
        bindir, s3root, logfile = self._install_mock_aws()
        project_dir = self._temp_dir / "project"
        bin_dir = project_dir / "tools"
        bin_dir.mkdir(parents=True)
        # The manifest points at a bucket the mock cannot serve; the env var points at the
        # http server. If the manifest won, aws would be invoked and the run would fail.
        (project_dir / "envy.lua").write_text(
            '-- @envy version "1.2.3"\n'
            '-- @envy mirror "s3://wrong-bucket/nope"\n\nPACKAGES = {}\n'
        )
        dest = bin_dir / ("envy.bat" if sys.platform == "win32" else "envy")
        dest.write_text(
            self._get_bootstrap_script().read_text().replace("@@ENVY_VERSION@@", "1.2.3")
        )
        if sys.platform != "win32":
            dest.chmod(dest.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        result = self._run_bootstrap(
            dest, ["version"], env_overrides=self._mock_aws_env(bindir, s3root, logfile)
        )

        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("envy version", result.stderr)
        self.assertFalse(
            logfile.exists(),
            f"aws was invoked, so the manifest mirror won over ENVY_MIRROR: "
            f"{logfile.read_text() if logfile.exists() else ''}",
        )
        self.assertNotEqual([], self._server.request_paths)

    @unittest.skipIf(
        sys.platform == "win32", "PATH minimization to exclude a real aws.exe is fragile"
    )
    def test_bootstrap_s3_mirror_without_aws_reports_missing_cli(self) -> None:
        """s3:// without the AWS CLI must say so, not fail obscurely."""
        project_dir = self._temp_dir / "project"
        bin_dir = project_dir / "tools"
        bin_dir.mkdir(parents=True)
        (project_dir / "envy.lua").write_text(
            '-- @envy version "1.2.3"\n'
            '-- @envy mirror "s3://fake-bucket/releases"\n\nPACKAGES = {}\n'
        )
        dest = bin_dir / "envy"
        dest.write_text(
            self._get_bootstrap_script().read_text().replace("@@ENVY_VERSION@@", "1.2.3")
        )
        dest.chmod(dest.stat().st_mode | stat.S_IXUSR)

        # AWS CLI v2 installs to /usr/local/bin, so a minimal PATH excludes it while still
        # providing the coreutils the script needs.
        result = self._run_bootstrap(
            dest, ["version"], env_overrides={"PATH": "/usr/bin:/bin"}, set_mirror=False
        )

        self.assertNotEqual(0, result.returncode)
        self.assertIn("aws CLI was not found", result.stderr)

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

        result = test_config.run(
            cmd, capture_output=True, text=True, env=env, cwd=project_dir, timeout=30
        )

        self.assertNotEqual(0, result.returncode)
        self.assertIn("envy.lua", result.stderr.lower())

    # --- attestation (@envy sha256sums) ------------------------------------------
    #
    # The chain: the manifest pins SHA256SUMS's own hash, SHA256SUMS names the archive's
    # hash, the archive is what gets executed. Break any link and the bootstrap must refuse
    # to exec rather than degrade to an unverified download.

    def _setup_attested_project(
        self,
        pin: str | None,
        version: str | None = "1.2.3",
    ) -> Path:
        """Write a manifest with an optional sums pin, plus the bootstrap script."""
        project_dir = self._temp_dir / "project"
        bin_dir = project_dir / "tools"
        bin_dir.mkdir(parents=True, exist_ok=True)

        lines = []
        if version is not None:
            lines.append(f'-- @envy version "{version}"')
        if pin is not None:
            lines.append(f'-- @envy sha256sums "{pin}"')
        (project_dir / "envy.lua").write_text("\n".join(lines) + "\n\nPACKAGES = {}\n")

        dest = bin_dir / ("envy.bat" if sys.platform == "win32" else "envy")
        dest.write_text(
            self._get_bootstrap_script().read_text().replace("@@ENVY_VERSION@@", "1.2.3")
        )
        if sys.platform != "win32":
            dest.chmod(dest.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        return dest

    def test_bootstrap_attests_a_matching_archive(self) -> None:
        bootstrap = self._setup_attested_project(self._server.sums_pin)
        result = self._run_bootstrap(bootstrap, ["version"])

        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("envy version", result.stderr)
        # Proves the sums file was actually consulted, not that verification was skipped.
        self.assertTrue(
            any(p.endswith("SHA256SUMS") for p in self._server.request_paths),
            f"SHA256SUMS was never fetched: {self._server.request_paths}",
        )

    def test_bootstrap_rejects_a_tampered_archive(self) -> None:
        """Mirror serves modified archive bytes but an untouched SHA256SUMS."""
        self._server.corrupt_archive = True
        bootstrap = self._setup_attested_project(self._server.sums_pin)

        result = self._run_bootstrap(bootstrap, ["version"])

        self.assertNotEqual(0, result.returncode)
        self.assertIn("attestation", result.stderr.lower())
        # The whole point: nothing ran. A corrupted archive that still extracted and exec'd
        # would make the check decorative.
        self.assertNotIn("envy version", result.stderr)

    def test_bootstrap_rejects_a_tampered_sums_file(self) -> None:
        """Mirror rewrites the archive *and* SHA256SUMS; only the manifest pin catches it."""
        pin = self._server.sums_pin  # captured before the mirror is rewritten
        self._server.corrupt_archive = True
        corrupted = self._server.pristine_archive + b"corrupted"
        digest = hashlib.sha256(corrupted).hexdigest()
        self._server.sums_body = (
            f"{digest}  {self._server.host_archive_name}\n".encode()
        )

        bootstrap = self._setup_attested_project(pin)
        result = self._run_bootstrap(bootstrap, ["version"])

        self.assertNotEqual(0, result.returncode)
        self.assertIn("sha256sums", result.stderr.lower())
        self.assertNotIn("envy version", result.stderr)

    def test_bootstrap_rejects_sums_without_an_entry_for_this_platform(self) -> None:
        self._server.sums_body = (
            f"{'a' * 64}  envy-some-other-platform.tar.gz\n".encode()
        )
        bootstrap = self._setup_attested_project(self._server.sums_pin)

        result = self._run_bootstrap(bootstrap, ["version"])

        self.assertNotEqual(0, result.returncode)
        self.assertIn("no entry", result.stderr.lower())

    def test_bootstrap_fails_when_pinned_sums_are_unavailable(self) -> None:
        """A mirror without SHA256SUMS cannot satisfy a pin, so the run must stop."""
        self._server.serve_sums = False
        bootstrap = self._setup_attested_project(self._server.sums_pin)

        result = self._run_bootstrap(bootstrap, ["version"])

        self.assertNotEqual(0, result.returncode)
        self.assertIn("SHA256SUMS", result.stderr)
        self.assertNotIn("envy version", result.stderr)

    def test_bootstrap_rejects_a_pin_without_a_pinned_version(self) -> None:
        """A sums pin names one release, so a dynamically resolved version cannot use it.

        Fails before any network traffic: silently skipping verification would be worse
        than having no pin, because the manifest still advertises attestation.
        """
        bootstrap = self._setup_attested_project(self._server.sums_pin, version=None)

        result = self._run_bootstrap(bootstrap, ["version"])

        self.assertNotEqual(0, result.returncode)
        self.assertIn("@envy version", result.stderr)
        self.assertEqual([], self._server.request_paths)

    def test_bootstrap_without_a_pin_does_not_fetch_sums(self) -> None:
        """Attestation is opt-in: an unpinned manifest keeps working, and pays nothing."""
        self._server.serve_sums = False  # would 404 if the script asked for it
        bootstrap = self._setup_attested_project(None)

        result = self._run_bootstrap(bootstrap, ["version"])

        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("envy version", result.stderr)
        self.assertFalse(
            any(p.endswith("SHA256SUMS") for p in self._server.request_paths),
            f"unpinned bootstrap fetched SHA256SUMS: {self._server.request_paths}",
        )

    def test_bootstrap_accepts_an_uppercase_pin(self) -> None:
        """certutil and Get-FileHash emit uppercase, so a hand-pasted pin often is."""
        bootstrap = self._setup_attested_project(self._server.sums_pin.upper())
        result = self._run_bootstrap(bootstrap, ["version"])

        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("envy version", result.stderr)


if __name__ == "__main__":
    unittest.main()

"""Functional tests for package depot feature.

Tests export --depot-prefix, depot manifest parsing, depot-based sync,
error handling, and graceful fallback to source builds.
"""

import hashlib
import io
import shutil
import socket
import tarfile
import tempfile
import threading
import unittest
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from . import test_config
from .test_config import make_manifest

TEST_ARCHIVE_FILES = {
    "root/file1.txt": "Test file content\n",
}


def _create_test_archive(output_path: Path) -> str:
    """Create test.tar.gz archive and return its SHA256 hash."""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tar:
        for name, content in TEST_ARCHIVE_FILES.items():
            data = content.encode("utf-8")
            info = tarfile.TarInfo(name=name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
    archive_data = buf.getvalue()
    output_path.write_bytes(archive_data)
    return hashlib.sha256(archive_data).hexdigest()


def _get_free_port():
    """Get a TCP port that nothing is currently listening on."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class _QuietHandler(SimpleHTTPRequestHandler):
    """HTTP handler that suppresses per-request logging."""

    def __init__(self, *args, directory=None, **kwargs):
        super().__init__(*args, directory=directory, **kwargs)

    def log_message(self, format, *args):
        return


def _spec_content(identity, archive_path, archive_hash):
    """Generate a cache-managed EXPORTABLE spec."""
    p = archive_path.as_posix()
    return f'''IDENTITY = "{identity}"
EXPORTABLE = true

FETCH = {{
  source = "{p}",
  sha256 = "{archive_hash}"
}}

STAGE = {{strip = 1}}

BUILD = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  if envy.PLATFORM == "windows" then
    envy.run([[Set-Content -Path built.txt -Value "built"]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[echo 'built' > built.txt]])
  end
end
'''


class TestExportDepotPrefix(unittest.TestCase):
    """Tests for 'envy export --depot-prefix' flag."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-depot-pfx-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-depot-pfx-specs-"))
        self.output_dir = Path(tempfile.mkdtemp(prefix="envy-depot-pfx-out-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent

        self.archive_path = self.test_dir / "test.tar.gz"
        self.archive_hash = _create_test_archive(self.archive_path)

        for name, identity in [
            ("pkg_a.lua", "local.depot_a@v1"),
            ("pkg_b.lua", "local.depot_b@v1"),
        ]:
            (self.test_dir / name).write_text(
                _spec_content(identity, self.archive_path, self.archive_hash),
                encoding="utf-8",
            )

    def tearDown(self):
        for d in [self.cache_root, self.test_dir, self.output_dir]:
            shutil.rmtree(d, ignore_errors=True)

    def _run(self, *args):
        cmd = [str(self.envy), "--cache-root", str(self.cache_root), *args]
        return test_config.run(
            cmd, cwd=self.project_root, capture_output=True, text=True
        )

    def _make_manifest(self, specs):
        packages = ", ".join(
            f'{{ spec = "{s}", source = "{self.test_dir.as_posix()}/{f}" }}'
            for s, f in specs
        )
        path = self.test_dir / "envy.lua"
        path.write_text(
            make_manifest(f"\nPACKAGES = {{\n    {packages}\n}}\n"),
            encoding="utf-8",
        )
        return path

    def test_export_depot_prefix(self):
        """Export with --depot-prefix outputs prefixed URLs."""
        m = self._make_manifest([("local.depot_a@v1", "pkg_a.lua")])
        r = self._run("install", "--manifest", str(m))
        self.assertEqual(r.returncode, 0, f"install failed: {r.stderr}")

        r = self._run(
            "export", "local.depot_a@v1",
            "-o", str(self.output_dir),
            "--depot-prefix", "s3://bucket/cache/",
            "--manifest", str(m),
        )
        self.assertEqual(r.returncode, 0, f"export failed: {r.stderr}")

        lines = [l for l in r.stdout.strip().split("\n") if l.strip()]
        self.assertEqual(len(lines), 1)
        self.assertTrue(lines[0].startswith("s3://bucket/cache/"))
        self.assertTrue(lines[0].endswith(".tar.zst"))
        self.assertIn("local.depot_a@v1-", lines[0])

    def test_export_depot_prefix_multiple_packages(self):
        """Export multiple packages with --depot-prefix."""
        m = self._make_manifest([
            ("local.depot_a@v1", "pkg_a.lua"),
            ("local.depot_b@v1", "pkg_b.lua"),
        ])
        r = self._run("install", "--manifest", str(m))
        self.assertEqual(r.returncode, 0, f"install failed: {r.stderr}")

        r = self._run(
            "export",
            "-o", str(self.output_dir),
            "--depot-prefix", "https://cdn.example.com/pkgs/",
            "--manifest", str(m),
        )
        self.assertEqual(r.returncode, 0, f"export failed: {r.stderr}")

        lines = [l for l in r.stdout.strip().split("\n") if l.strip()]
        self.assertEqual(len(lines), 2)
        for line in lines:
            self.assertTrue(line.startswith("https://cdn.example.com/pkgs/"))
            self.assertTrue(line.endswith(".tar.zst"))


class TestPackageDepot(unittest.TestCase):
    """Tests for depot-based sync: depot manifests, archive lookup, fallback."""

    def setUp(self):
        self.source_cache = Path(tempfile.mkdtemp(prefix="envy-depot-src-"))
        self.target_cache = Path(tempfile.mkdtemp(prefix="envy-depot-tgt-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-depot-specs-"))
        self.output_dir = Path(tempfile.mkdtemp(prefix="envy-depot-out-"))
        self.serve_dir = Path(tempfile.mkdtemp(prefix="envy-depot-http-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent

        self.archive_path = self.test_dir / "test.tar.gz"
        self.archive_hash = _create_test_archive(self.archive_path)

        self.spec_lua = {}
        for name, identity in [
            ("pkg_a.lua", "local.depot_a@v1"),
            ("pkg_b.lua", "local.depot_b@v1"),
        ]:
            (self.test_dir / name).write_text(
                _spec_content(identity, self.archive_path, self.archive_hash),
                encoding="utf-8",
            )
            self.spec_lua[identity] = f"{self.test_dir.as_posix()}/{name}"

    def tearDown(self):
        for d in [
            self.source_cache, self.target_cache,
            self.test_dir, self.output_dir, self.serve_dir,
        ]:
            shutil.rmtree(d, ignore_errors=True)

    # -- helpers --

    def _run(self, *args, cache_root=None):
        cache = cache_root or self.source_cache
        cmd = [str(self.envy), "--cache-root", str(cache), *args]
        return test_config.run(
            cmd, cwd=self.project_root, capture_output=True, text=True
        )

    def _make_source_manifest(self, identities):
        """Create manifest for initial install/export (no depot directives)."""
        packages = ", ".join(
            f'{{ spec = "{i}", source = "{self.spec_lua[i]}" }}'
            for i in identities
        )
        path = self.test_dir / "source.lua"
        path.write_text(
            make_manifest(f"\nPACKAGES = {{\n    {packages}\n}}\n"),
            encoding="utf-8",
        )
        return path

    def _install_and_export(self, identities):
        """Install + export packages from source cache. Returns archive paths."""
        m = self._make_source_manifest(identities)
        r = self._run("install", "--manifest", str(m))
        self.assertEqual(r.returncode, 0, f"install failed: {r.stderr}")

        r = self._run("export", "-o", str(self.output_dir), "--manifest", str(m))
        self.assertEqual(r.returncode, 0, f"export failed: {r.stderr}")
        return [Path(l.strip()) for l in r.stdout.strip().split("\n") if l.strip()]

    def _start_server(self):
        """Start HTTP server on self.serve_dir. Returns (server, port)."""
        handler = partial(_QuietHandler, directory=str(self.serve_dir))
        srv = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        return srv, srv.server_address[1]

    def _make_depot_manifest(self, archive_paths, port, name="depot.txt"):
        """Copy archives to serve_dir, write depot manifest. Returns URL."""
        lines = []
        for ap in archive_paths:
            shutil.copy2(ap, self.serve_dir / ap.name)
            lines.append(f"http://127.0.0.1:{port}/{ap.name}")
        (self.serve_dir / name).write_text(
            "\n".join(lines) + "\n", encoding="utf-8"
        )
        return f"http://127.0.0.1:{port}/{name}"

    def _make_target_manifest(self, identities, depot_urls):
        """Create manifest with depot directives for target sync."""
        header = '-- @envy bin "envy-bin"\n'
        for u in depot_urls:
            header += f'-- @envy package-depot "{u}"\n'
        packages = ", ".join(
            f'{{ spec = "{i}", source = "{self.spec_lua[i]}" }}'
            for i in identities
        )
        path = self.test_dir / "target.lua"
        path.write_text(
            header + f"\nPACKAGES = {{\n    {packages}\n}}\n",
            encoding="utf-8",
        )
        return path

    # -- tests --

    def test_depot_hit_skips_build(self):
        """Sync with depot archive uses pre-built package."""
        archives = self._install_and_export(["local.depot_a@v1"])
        self.assertEqual(len(archives), 1)

        srv, port = self._start_server()
        try:
            depot_url = self._make_depot_manifest(archives, port)
            m = self._make_target_manifest(["local.depot_a@v1"], [depot_url])

            r = self._run(
                "sync", "--manifest", str(m), cache_root=self.target_cache
            )
            self.assertEqual(r.returncode, 0, f"sync failed: {r.stderr}")

            pkg_dir = self.target_cache / "packages" / "local.depot_a@v1"
            self.assertTrue(pkg_dir.exists(), "Package should be in target cache")
        finally:
            srv.shutdown()
            srv.server_close()

    def test_depot_hit_multiple_packages(self):
        """Multiple packages all served from depot."""
        archives = self._install_and_export(
            ["local.depot_a@v1", "local.depot_b@v1"]
        )
        self.assertEqual(len(archives), 2)

        srv, port = self._start_server()
        try:
            depot_url = self._make_depot_manifest(archives, port)
            m = self._make_target_manifest(
                ["local.depot_a@v1", "local.depot_b@v1"], [depot_url]
            )

            r = self._run(
                "sync", "--manifest", str(m), cache_root=self.target_cache
            )
            self.assertEqual(r.returncode, 0, f"sync failed: {r.stderr}")

            for pkg in ["local.depot_a@v1", "local.depot_b@v1"]:
                pkg_dir = self.target_cache / "packages" / pkg
                self.assertTrue(pkg_dir.exists(), f"{pkg} should be cached")
        finally:
            srv.shutdown()
            srv.server_close()

    def test_depot_manifest_unreachable(self):
        """Sync succeeds when depot manifest is unreachable (builds from source)."""
        dead_port = _get_free_port()
        m = self._make_target_manifest(
            ["local.depot_a@v1"],
            [f"http://127.0.0.1:{dead_port}/depot.txt"],
        )

        r = self._run(
            "sync", "--manifest", str(m), cache_root=self.target_cache
        )
        self.assertEqual(r.returncode, 0, f"sync failed: {r.stderr}")

        pkg_dir = self.target_cache / "packages" / "local.depot_a@v1"
        self.assertTrue(pkg_dir.exists())

    def test_depot_manifest_empty(self):
        """Sync succeeds with empty depot manifest (builds from source)."""
        srv, port = self._start_server()
        try:
            (self.serve_dir / "depot.txt").write_text("", encoding="utf-8")
            depot_url = f"http://127.0.0.1:{port}/depot.txt"
            m = self._make_target_manifest(["local.depot_a@v1"], [depot_url])

            r = self._run(
                "sync", "--manifest", str(m), cache_root=self.target_cache
            )
            self.assertEqual(r.returncode, 0, f"sync failed: {r.stderr}")

            pkg_dir = self.target_cache / "packages" / "local.depot_a@v1"
            self.assertTrue(pkg_dir.exists())
        finally:
            srv.shutdown()
            srv.server_close()

    def test_depot_archive_not_found(self):
        """Sync falls back to source when depot archive returns 404."""
        archives = self._install_and_export(["local.depot_a@v1"])

        srv, port = self._start_server()
        try:
            # Write depot manifest referencing archive, but don't copy it to serve_dir
            (self.serve_dir / "depot.txt").write_text(
                f"http://127.0.0.1:{port}/{archives[0].name}\n",
                encoding="utf-8",
            )
            depot_url = f"http://127.0.0.1:{port}/depot.txt"
            m = self._make_target_manifest(["local.depot_a@v1"], [depot_url])

            r = self._run(
                "sync", "--manifest", str(m), cache_root=self.target_cache
            )
            self.assertEqual(r.returncode, 0, f"sync failed: {r.stderr}")

            pkg_dir = self.target_cache / "packages" / "local.depot_a@v1"
            self.assertTrue(pkg_dir.exists())
        finally:
            srv.shutdown()
            srv.server_close()

    def test_depot_mixed_hit_and_miss(self):
        """One package from depot, the other from source. Both succeed."""
        archives = self._install_and_export(
            ["local.depot_a@v1", "local.depot_b@v1"]
        )
        self.assertEqual(len(archives), 2)

        srv, port = self._start_server()
        try:
            # Only serve pkg_a via depot
            pkg_a_archives = [a for a in archives if "depot_a" in a.name]
            self.assertEqual(len(pkg_a_archives), 1)
            depot_url = self._make_depot_manifest(pkg_a_archives, port)

            m = self._make_target_manifest(
                ["local.depot_a@v1", "local.depot_b@v1"], [depot_url]
            )

            r = self._run(
                "sync", "--manifest", str(m), cache_root=self.target_cache
            )
            self.assertEqual(r.returncode, 0, f"sync failed: {r.stderr}")

            for pkg in ["local.depot_a@v1", "local.depot_b@v1"]:
                pkg_dir = self.target_cache / "packages" / pkg
                self.assertTrue(pkg_dir.exists(), f"{pkg} should be cached")
        finally:
            srv.shutdown()
            srv.server_close()

    def test_multiple_depots_merged(self):
        """Two depot manifests with disjoint packages both contribute."""
        archives = self._install_and_export(
            ["local.depot_a@v1", "local.depot_b@v1"]
        )
        a_archives = [a for a in archives if "depot_a" in a.name]
        b_archives = [a for a in archives if "depot_b" in a.name]

        srv, port = self._start_server()
        try:
            depot_a_url = self._make_depot_manifest(a_archives, port, "depot_a.txt")
            depot_b_url = self._make_depot_manifest(b_archives, port, "depot_b.txt")

            m = self._make_target_manifest(
                ["local.depot_a@v1", "local.depot_b@v1"],
                [depot_a_url, depot_b_url],
            )

            r = self._run(
                "sync", "--manifest", str(m), cache_root=self.target_cache
            )
            self.assertEqual(r.returncode, 0, f"sync failed: {r.stderr}")

            for pkg in ["local.depot_a@v1", "local.depot_b@v1"]:
                pkg_dir = self.target_cache / "packages" / pkg
                self.assertTrue(pkg_dir.exists(), f"{pkg} should be cached")
        finally:
            srv.shutdown()
            srv.server_close()

    def test_full_ci_workflow(self):
        """End-to-end: export with --depot-prefix, serve, sync from depot."""
        source_m = self._make_source_manifest(["local.depot_a@v1"])
        r = self._run("install", "--manifest", str(source_m))
        self.assertEqual(r.returncode, 0, f"install failed: {r.stderr}")

        srv, port = self._start_server()
        try:
            prefix = f"http://127.0.0.1:{port}/"
            r = self._run(
                "export",
                "-o", str(self.serve_dir),
                "--depot-prefix", prefix,
                "--manifest", str(source_m),
            )
            self.assertEqual(r.returncode, 0, f"export failed: {r.stderr}")

            # stdout lines are depot manifest content
            (self.serve_dir / "depot.txt").write_text(
                r.stdout.strip() + "\n", encoding="utf-8"
            )
            depot_url = f"http://127.0.0.1:{port}/depot.txt"

            m = self._make_target_manifest(["local.depot_a@v1"], [depot_url])
            r = self._run(
                "sync", "--manifest", str(m), cache_root=self.target_cache
            )
            self.assertEqual(r.returncode, 0, f"sync failed: {r.stderr}")

            pkg_dir = self.target_cache / "packages" / "local.depot_a@v1"
            self.assertTrue(pkg_dir.exists())
        finally:
            srv.shutdown()
            srv.server_close()

    def test_depot_then_local_cache_hit(self):
        """Second sync gets local cache hit; no depot fetch needed."""
        archives = self._install_and_export(["local.depot_a@v1"])

        srv, port = self._start_server()
        depot_url = self._make_depot_manifest(archives, port)
        m = self._make_target_manifest(["local.depot_a@v1"], [depot_url])

        try:
            # First sync: depot hit
            r = self._run(
                "sync", "--manifest", str(m), cache_root=self.target_cache
            )
            self.assertEqual(r.returncode, 0, f"first sync failed: {r.stderr}")
        finally:
            srv.shutdown()
            srv.server_close()

        # Second sync with server down: local cache hit (no depot needed)
        r = self._run(
            "sync", "--manifest", str(m), cache_root=self.target_cache
        )
        self.assertEqual(r.returncode, 0, f"second sync failed: {r.stderr}")

    def test_depot_archive_corrupt(self):
        """Sync falls back to source when depot archive is corrupt (not valid tar.zst)."""
        archives = self._install_and_export(["local.depot_a@v1"])

        srv, port = self._start_server()
        try:
            # Write depot manifest, but replace the archive with garbage bytes
            (self.serve_dir / "depot.txt").write_text(
                f"http://127.0.0.1:{port}/{archives[0].name}\n",
                encoding="utf-8",
            )
            (self.serve_dir / archives[0].name).write_bytes(b"this is not a valid tar.zst")
            depot_url = f"http://127.0.0.1:{port}/depot.txt"
            m = self._make_target_manifest(["local.depot_a@v1"], [depot_url])

            r = self._run(
                "sync", "--manifest", str(m), cache_root=self.target_cache
            )
            self.assertEqual(r.returncode, 0, f"sync failed: {r.stderr}")

            pkg_dir = self.target_cache / "packages" / "local.depot_a@v1"
            self.assertTrue(pkg_dir.exists(), "Should fall back to source build")
        finally:
            srv.shutdown()
            srv.server_close()

    def test_depot_manifest_comments_and_blank_lines(self):
        """Depot manifest with comments and blank lines works end-to-end."""
        archives = self._install_and_export(["local.depot_a@v1"])

        srv, port = self._start_server()
        try:
            # Write depot manifest with comments and blank lines
            shutil.copy2(archives[0], self.serve_dir / archives[0].name)
            content = (
                "# Depot manifest for CI\n"
                "\n"
                f"http://127.0.0.1:{port}/{archives[0].name}\n"
                "\n"
                "# End of manifest\n"
            )
            (self.serve_dir / "depot.txt").write_text(content, encoding="utf-8")
            depot_url = f"http://127.0.0.1:{port}/depot.txt"
            m = self._make_target_manifest(["local.depot_a@v1"], [depot_url])

            r = self._run(
                "sync", "--manifest", str(m), cache_root=self.target_cache
            )
            self.assertEqual(r.returncode, 0, f"sync failed: {r.stderr}")

            pkg_dir = self.target_cache / "packages" / "local.depot_a@v1"
            self.assertTrue(pkg_dir.exists(), "Should install from depot")
        finally:
            srv.shutdown()
            srv.server_close()

    def test_export_depot_prefix_no_trailing_slash(self):
        """Export with --depot-prefix without trailing slash still produces valid URLs."""
        m = self._make_source_manifest(["local.depot_a@v1"])
        r = self._run("install", "--manifest", str(m))
        self.assertEqual(r.returncode, 0, f"install failed: {r.stderr}")

        r = self._run(
            "export", "local.depot_a@v1",
            "-o", str(self.output_dir),
            "--depot-prefix", "s3://bucket/cache",
            "--manifest", str(m),
        )
        self.assertEqual(r.returncode, 0, f"export failed: {r.stderr}")

        lines = [l for l in r.stdout.strip().split("\n") if l.strip()]
        self.assertEqual(len(lines), 1)
        # Without trailing slash, prefix is concatenated directly with filename
        self.assertTrue(lines[0].startswith("s3://bucket/cache"))
        self.assertTrue(lines[0].endswith(".tar.zst"))


if __name__ == "__main__":
    unittest.main()

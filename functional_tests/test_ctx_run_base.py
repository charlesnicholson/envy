"""Shared infrastructure for envy.run() tests.

Provides base class with common setUp/tearDown, archive creation,
and spec writing utilities for ctx.run test files.
"""

import hashlib
import io
import os
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path
import unittest

from . import test_config

# Test archive contents - matches original test.tar.gz structure
TEST_ARCHIVE_FILES = {
    "root/file1.txt": "Hello, world!\n",
    "root/file2.txt": "Second file content\n",
}


def create_test_archive(output_path: Path) -> str:
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


class CtxRunTestBase(unittest.TestCase):
    """Base class for envy.run() tests."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-ctx-run-test-"))
        self.specs_dir = Path(tempfile.mkdtemp(prefix="envy-ctx-run-specs-"))
        self.envy_test = test_config.get_envy_executable()
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

        # Create test archive and get its hash
        self.archive_path = self.specs_dir / "test.tar.gz"
        self.archive_hash = create_test_archive(self.archive_path)
        self.archive_lua_path = self.archive_path.as_posix()

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.specs_dir, ignore_errors=True)

    def write_spec(self, name: str, content: str) -> None:
        """Write spec to temp dir with placeholder substitution."""
        spec_content = content.format(
            ARCHIVE_PATH=self.archive_lua_path,
            ARCHIVE_HASH=self.archive_hash,
        )
        (self.specs_dir / name).write_text(spec_content, encoding="utf-8")

    def get_pkg_path(self, identity):
        """Find package directory for given identity in cache."""
        pkgs_dir = self.cache_root / "packages" / identity
        if not pkgs_dir.exists():
            return None
        for subdir in pkgs_dir.iterdir():
            if subdir.is_dir():
                pkg_dir = subdir / "pkg"
                if pkg_dir.exists():
                    return pkg_dir
                return subdir
        return None

    def run_spec(self, identity, spec_name, should_fail=False):
        """Run a spec and return result."""
        spec_path = str(self.specs_dir / spec_name)
        result = test_config.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                identity,
                spec_path,
            ],
            capture_output=True,
            text=True,
        )

        if should_fail:
            self.assertNotEqual(
                result.returncode,
                0,
                f"Expected failure but succeeded.\nstdout: {result.stdout}\nstderr: {result.stderr}",
            )
        else:
            self.assertEqual(
                result.returncode,
                0,
                f"stdout: {result.stdout}\nstderr: {result.stderr}",
            )

        return result

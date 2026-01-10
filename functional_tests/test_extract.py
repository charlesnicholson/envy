from __future__ import annotations

import bz2
import gzip
import io
import lzma
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

from . import test_config

# Test file structure: root/{file1.txt, file2.txt, subdir1/{file3.txt, nested/file4.txt}, subdir2/file5.txt}
TEST_FILES = {
    "root/file1.txt": "Root file content\n",
    "root/file2.txt": "Another root file\n",
    "root/subdir1/file3.txt": "Subdirectory file\n",
    "root/subdir1/nested/file4.txt": "Nested file content\n",
    "root/subdir2/file5.txt": "Second subdir file\n",
}


def create_tar_archive(output_path: Path, compression: str | None = None) -> None:
    """Create a tar archive with test files."""
    mode = "w"
    if compression == "gz":
        mode = "w:gz"
    elif compression == "bz2":
        mode = "w:bz2"
    elif compression == "xz":
        mode = "w:xz"

    with tarfile.open(output_path, mode) as tar:
        for name, content in TEST_FILES.items():
            data = content.encode("utf-8")
            info = tarfile.TarInfo(name=name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))


def create_tar_zst_archive(output_path: Path) -> None:
    """Create a zstd-compressed tar archive."""
    try:
        import zstandard as zstd
    except ImportError:
        # Fall back to creating via subprocess if zstandard not available
        tar_path = output_path.with_suffix("")
        create_tar_archive(tar_path)
        subprocess.run(["zstd", "-f", str(tar_path), "-o", str(output_path)], check=True)
        tar_path.unlink()
        return

    # Create tar in memory
    tar_buffer = io.BytesIO()
    with tarfile.open(fileobj=tar_buffer, mode="w") as tar:
        for name, content in TEST_FILES.items():
            data = content.encode("utf-8")
            info = tarfile.TarInfo(name=name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))

    # Compress with zstd
    cctx = zstd.ZstdCompressor()
    compressed = cctx.compress(tar_buffer.getvalue())
    output_path.write_bytes(compressed)


def create_zip_archive(output_path: Path) -> None:
    """Create a zip archive with test files (no root/ prefix)."""
    with zipfile.ZipFile(output_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for name, content in TEST_FILES.items():
            # Strip 'root/' prefix for zip format
            zip_name = name.replace("root/", "", 1)
            zf.writestr(zip_name, content)


class EnvyExtractTests(unittest.TestCase):
    def setUp(self) -> None:
        self._envy_binary = test_config.get_envy_executable()
        self._tmpdir = tempfile.mkdtemp(prefix="envy-extract-test-")
        self._archives_dir = Path(self._tmpdir) / "archives"
        self._archives_dir.mkdir()

        # Create test archives
        create_tar_archive(self._archives_dir / "test.tar")
        create_tar_archive(self._archives_dir / "test.tar.gz", compression="gz")
        create_tar_archive(self._archives_dir / "test.tar.bz2", compression="bz2")
        create_tar_archive(self._archives_dir / "test.tar.xz", compression="xz")
        create_zip_archive(self._archives_dir / "test.zip")

    def tearDown(self) -> None:
        import shutil
        shutil.rmtree(self._tmpdir, ignore_errors=True)

    def _run_envy(self, *args: str, cwd: str | None = None) -> subprocess.CompletedProcess[str]:
        self.assertTrue(
            self._envy_binary.exists(), f"Expected envy binary at {self._envy_binary}"
        )
        env = os.environ.copy()
        return subprocess.run(
            [str(self._envy_binary), *args],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env,
            cwd=cwd,
        )

    def _verify_extracted_structure(self, extract_dir: Path) -> None:
        """Verify the expected directory structure and file contents after extraction."""
        # For tar archives, the structure includes 'root/' prefix
        # For zip archive created with relative paths, files are at the top level

        # Check if we have the root directory (tar format)
        root_dir = extract_dir / "root"
        if root_dir.exists():
            base = root_dir
        else:
            # Zip format - files at top level
            base = extract_dir

        # Verify all expected files exist
        file1 = base / "file1.txt"
        file2 = base / "file2.txt"
        file3 = base / "subdir1" / "file3.txt"
        file4 = base / "subdir1" / "nested" / "file4.txt"
        file5 = base / "subdir2" / "file5.txt"

        self.assertTrue(file1.exists(), f"Expected {file1} to exist")
        self.assertTrue(file2.exists(), f"Expected {file2} to exist")
        self.assertTrue(file3.exists(), f"Expected {file3} to exist")
        self.assertTrue(file4.exists(), f"Expected {file4} to exist")
        self.assertTrue(file5.exists(), f"Expected {file5} to exist")

        # Verify file contents
        self.assertEqual("Root file content\n", file1.read_text())
        self.assertEqual("Another root file\n", file2.read_text())
        self.assertEqual("Subdirectory file\n", file3.read_text())
        self.assertEqual("Nested file content\n", file4.read_text())
        self.assertEqual("Second subdir file\n", file5.read_text())

        # Verify directories exist
        self.assertTrue((base / "subdir1").is_dir())
        self.assertTrue((base / "subdir2").is_dir())
        self.assertTrue((base / "subdir1" / "nested").is_dir())

    def test_extract_missing_archive_fails(self) -> None:
        """Test that extracting a non-existent archive fails with appropriate error."""
        with tempfile.TemporaryDirectory() as tmpdir:
            result = self._run_envy("extract", "nonexistent.tar.gz", tmpdir)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not exist", result.stderr)

    def test_extract_tar(self) -> None:
        """Test extracting a plain .tar archive."""
        archive = self._archives_dir / "test.tar"
        self.assertTrue(archive.exists(), f"Test archive {archive} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            result = self._run_envy("extract", str(archive), tmpdir)

            self.assertEqual(0, result.returncode, f"Extract failed: {result.stderr}")
            self.assertIn("Extracted", result.stderr)
            self.assertIn("files", result.stderr)

            self._verify_extracted_structure(Path(tmpdir))

    def test_extract_tar_gz(self) -> None:
        """Test extracting a .tar.gz (gzip compressed tar) archive."""
        archive = self._archives_dir / "test.tar.gz"
        self.assertTrue(archive.exists(), f"Test archive {archive} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            result = self._run_envy("extract", str(archive), tmpdir)

            self.assertEqual(0, result.returncode, f"Extract failed: {result.stderr}")
            self.assertIn("Extracted", result.stderr)

            self._verify_extracted_structure(Path(tmpdir))

    def test_extract_tar_bz2(self) -> None:
        """Test extracting a .tar.bz2 (bzip2 compressed tar) archive."""
        archive = self._archives_dir / "test.tar.bz2"
        self.assertTrue(archive.exists(), f"Test archive {archive} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            result = self._run_envy("extract", str(archive), tmpdir)

            self.assertEqual(0, result.returncode, f"Extract failed: {result.stderr}")
            self.assertIn("Extracted", result.stderr)

            self._verify_extracted_structure(Path(tmpdir))

    def test_extract_tar_xz(self) -> None:
        """Test extracting a .tar.xz (xz/lzma compressed tar) archive."""
        archive = self._archives_dir / "test.tar.xz"
        self.assertTrue(archive.exists(), f"Test archive {archive} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            result = self._run_envy("extract", str(archive), tmpdir)

            self.assertEqual(0, result.returncode, f"Extract failed: {result.stderr}")
            self.assertIn("Extracted", result.stderr)

            self._verify_extracted_structure(Path(tmpdir))

    @unittest.skip("zstd archive creation requires zstandard module or zstd binary")
    def test_extract_tar_zst(self) -> None:
        """Test extracting a .tar.zst (zstd compressed tar) archive."""
        archive = self._archives_dir / "test.tar.zst"
        create_tar_zst_archive(archive)
        self.assertTrue(archive.exists(), f"Test archive {archive} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            result = self._run_envy("extract", str(archive), tmpdir)

            self.assertEqual(0, result.returncode, f"Extract failed: {result.stderr}")
            self.assertIn("Extracted", result.stderr)

            self._verify_extracted_structure(Path(tmpdir))

    def test_extract_zip(self) -> None:
        """Test extracting a .zip archive."""
        archive = self._archives_dir / "test.zip"
        self.assertTrue(archive.exists(), f"Test archive {archive} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            result = self._run_envy("extract", str(archive), tmpdir)

            self.assertEqual(0, result.returncode, f"Extract failed: {result.stderr}")
            self.assertIn("Extracted", result.stderr)

            self._verify_extracted_structure(Path(tmpdir))

    def test_extract_to_current_directory(self) -> None:
        """Test extracting to current directory when destination is not specified."""
        archive = self._archives_dir / "test.tar.gz"
        self.assertTrue(archive.exists(), f"Test archive {archive} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            result = self._run_envy("extract", str(archive), cwd=tmpdir)

            self.assertEqual(0, result.returncode, f"Extract failed: {result.stderr}")
            self.assertIn("Extracted", result.stderr)

            self._verify_extracted_structure(Path(tmpdir))

    def test_extract_creates_destination_if_missing(self) -> None:
        """Test that extract creates the destination directory if it doesn't exist."""
        archive = self._archives_dir / "test.tar.gz"
        self.assertTrue(archive.exists(), f"Test archive {archive} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            dest = Path(tmpdir) / "nested" / "destination"
            self.assertFalse(dest.exists())

            result = self._run_envy("extract", str(archive), str(dest))

            self.assertEqual(0, result.returncode, f"Extract failed: {result.stderr}")
            self.assertTrue(dest.exists())
            self.assertTrue(dest.is_dir())

            self._verify_extracted_structure(dest)

    def test_extract_reports_file_count(self) -> None:
        """Test that extract reports the number of files extracted."""
        archive = self._archives_dir / "test.tar.gz"
        self.assertTrue(archive.exists(), f"Test archive {archive} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            result = self._run_envy("extract", str(archive), tmpdir)

            self.assertEqual(0, result.returncode)
            self.assertIn("Extracted 5 files", result.stderr)


if __name__ == "__main__":
    unittest.main()

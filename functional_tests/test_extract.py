from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class EnvyExtractTests(unittest.TestCase):
    def setUp(self) -> None:
        self._project_root = Path(__file__).resolve().parent.parent
        binary_name = "envy.exe" if sys.platform == "win32" else "envy"
        self._envy_binary = self._project_root / "out" / "build" / binary_name
        self._test_archives = self._project_root / "test_data" / "archives"

    def _run_envy(self, *args: str) -> subprocess.CompletedProcess[str]:
        self.assertTrue(
            self._envy_binary.exists(), f"Expected envy binary at {self._envy_binary}"
        )
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
        archive = self._test_archives / "test.tar"
        self.assertTrue(archive.exists(), f"Test archive {archive} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            result = self._run_envy("extract", str(archive), tmpdir)

            self.assertEqual(0, result.returncode, f"Extract failed: {result.stderr}")
            self.assertIn("Extracted", result.stderr)
            self.assertIn("files", result.stderr)

            self._verify_extracted_structure(Path(tmpdir))

    def test_extract_tar_gz(self) -> None:
        """Test extracting a .tar.gz (gzip compressed tar) archive."""
        archive = self._test_archives / "test.tar.gz"
        self.assertTrue(archive.exists(), f"Test archive {archive} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            result = self._run_envy("extract", str(archive), tmpdir)

            self.assertEqual(0, result.returncode, f"Extract failed: {result.stderr}")
            self.assertIn("Extracted", result.stderr)

            self._verify_extracted_structure(Path(tmpdir))

    def test_extract_tar_bz2(self) -> None:
        """Test extracting a .tar.bz2 (bzip2 compressed tar) archive."""
        archive = self._test_archives / "test.tar.bz2"
        self.assertTrue(archive.exists(), f"Test archive {archive} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            result = self._run_envy("extract", str(archive), tmpdir)

            self.assertEqual(0, result.returncode, f"Extract failed: {result.stderr}")
            self.assertIn("Extracted", result.stderr)

            self._verify_extracted_structure(Path(tmpdir))

    def test_extract_tar_xz(self) -> None:
        """Test extracting a .tar.xz (xz/lzma compressed tar) archive."""
        archive = self._test_archives / "test.tar.xz"
        self.assertTrue(archive.exists(), f"Test archive {archive} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            result = self._run_envy("extract", str(archive), tmpdir)

            self.assertEqual(0, result.returncode, f"Extract failed: {result.stderr}")
            self.assertIn("Extracted", result.stderr)

            self._verify_extracted_structure(Path(tmpdir))

    def test_extract_tar_zst(self) -> None:
        """Test extracting a .tar.zst (zstd compressed tar) archive."""
        archive = self._test_archives / "test.tar.zst"
        self.assertTrue(archive.exists(), f"Test archive {archive} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            result = self._run_envy("extract", str(archive), tmpdir)

            self.assertEqual(0, result.returncode, f"Extract failed: {result.stderr}")
            self.assertIn("Extracted", result.stderr)

            self._verify_extracted_structure(Path(tmpdir))

    def test_extract_zip(self) -> None:
        """Test extracting a .zip archive."""
        archive = self._test_archives / "test.zip"
        self.assertTrue(archive.exists(), f"Test archive {archive} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            result = self._run_envy("extract", str(archive), tmpdir)

            self.assertEqual(0, result.returncode, f"Extract failed: {result.stderr}")
            self.assertIn("Extracted", result.stderr)

            self._verify_extracted_structure(Path(tmpdir))

    def test_extract_to_current_directory(self) -> None:
        """Test extracting to current directory when destination is not specified."""
        archive = self._test_archives / "test.tar.gz"
        self.assertTrue(archive.exists(), f"Test archive {archive} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            # Run envy from within tmpdir without specifying destination
            env = os.environ.copy()
            env.setdefault("ENVY_CACHE_DIR", str(self._project_root / "out" / "cache"))
            result = subprocess.run(
                [str(self._envy_binary), "extract", str(archive)],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                env=env,
                cwd=tmpdir,
            )

            self.assertEqual(0, result.returncode, f"Extract failed: {result.stderr}")
            self.assertIn("Extracted", result.stderr)

            self._verify_extracted_structure(Path(tmpdir))

    def test_extract_creates_destination_if_missing(self) -> None:
        """Test that extract creates the destination directory if it doesn't exist."""
        archive = self._test_archives / "test.tar.gz"
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
        archive = self._test_archives / "test.tar.gz"
        self.assertTrue(archive.exists(), f"Test archive {archive} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            result = self._run_envy("extract", str(archive), tmpdir)

            self.assertEqual(0, result.returncode)
            self.assertIn("Extracted 5 files", result.stderr)


if __name__ == "__main__":
    unittest.main()

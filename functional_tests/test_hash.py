#!/usr/bin/env python3
"""Functional tests for hash command."""

import base64
import hashlib
import subprocess
import tempfile
import unittest
from pathlib import Path

from . import test_config

# 1x1 red PNG (69 bytes)
TEST_PNG_BASE64 = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVQImQEBAP7///8AAAEABQABAAAAAElFTkSuQmCC"
TEST_PNG_SHA256 = "4ef9eb6a6a63f6cb017233d2ee2087ae5e13787801e969ef8150a2c008c5795a"


class TestHash(unittest.TestCase):
    """Tests for 'envy hash' command."""

    def setUp(self):
        self.envy = test_config.get_envy_executable()
        self.tmpdir = tempfile.mkdtemp(prefix="envy-hash-test-")

    def tearDown(self):
        import shutil

        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_hash_binary_file_matches_external_tool(self):
        """Verify envy hash matches external SHA256 computation (ground truth)."""
        # Write test PNG to temp file
        test_file = Path(self.tmpdir) / "test.png"
        test_file.write_bytes(base64.b64decode(TEST_PNG_BASE64))

        # Compute expected hash with Python's hashlib (ground truth)
        with open(test_file, "rb") as f:
            expected_hash = hashlib.sha256(f.read()).hexdigest()

        # Verify against hardcoded expected value
        self.assertEqual(
            expected_hash,
            TEST_PNG_SHA256,
            "Test PNG SHA256 mismatch - check base64 encoding",
        )

        # Compute hash with envy
        result = subprocess.run(
            [str(self.envy), "hash", str(test_file)],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"Hash command failed: {result.stderr}")
        envy_hash = result.stdout.strip()

        # Verify envy hash matches Python hashlib (ground truth)
        self.assertEqual(
            envy_hash,
            expected_hash,
            f"envy hash doesn't match Python hashlib: {envy_hash} != {expected_hash}",
        )

    def test_hash_nonexistent_file_fails(self):
        """Hash command fails gracefully on nonexistent file."""
        result = subprocess.run(
            [str(self.envy), "hash", "/nonexistent/file.txt"],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(result.returncode, 0, "Should fail on nonexistent file")
        self.assertIn("not exist", result.stderr.lower())

    def test_hash_directory_fails(self):
        """Hash command fails gracefully on directory."""
        result = subprocess.run(
            [str(self.envy), "hash", self.tmpdir],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(result.returncode, 0, "Should fail on directory")
        self.assertIn("directory", result.stderr.lower())

    def test_hash_missing_argument_fails(self):
        """Hash command fails when file argument is missing."""
        result = subprocess.run(
            [str(self.envy), "hash"],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(result.returncode, 0, "Should fail when argument missing")


if __name__ == "__main__":
    unittest.main()

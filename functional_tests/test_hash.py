#!/usr/bin/env python3
"""Functional tests for hash command."""

import hashlib
import subprocess
import sys
import unittest
from pathlib import Path

from . import test_config


class TestHash(unittest.TestCase):
    """Tests for 'envy hash' command."""

    def setUp(self):
        self.project_root = Path(__file__).resolve().parent.parent
        self.envy = test_config.get_envy_executable()

    def test_hash_binary_file_matches_external_tool(self):
        """Verify envy hash matches external SHA256 computation (ground truth)."""
        # Use binary test file to avoid any line-ending issues
        test_file = self.project_root / "test_data" / "binary" / "test.png"
        self.assertTrue(test_file.exists(), f"Test file not found: {test_file}")

        # Compute expected hash with Python's hashlib (ground truth)
        with open(test_file, "rb") as f:
            expected_hash = hashlib.sha256(f.read()).hexdigest()

        # Hardcoded expected value for test.png (verified with shasum -a 256)
        # This ensures we catch any platform-specific issues
        self.assertEqual(
            expected_hash,
            "4ef9eb6a6a63f6cb017233d2ee2087ae5e13787801e969ef8150a2c008c5795a",
            "Test file SHA256 changed - regenerate test.png or update expected hash",
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
        test_dir = self.project_root / "test_data"
        result = subprocess.run(
            [str(self.envy), "hash", str(test_dir)],
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

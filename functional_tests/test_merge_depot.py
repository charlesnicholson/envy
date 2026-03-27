"""Functional tests for 'envy merge-depot' command."""

import shutil
import tempfile
import unittest
from pathlib import Path

from . import test_config

# Canonical 64-char hex hashes for test use
HASH_A = "a" * 64
HASH_B = "b" * 64
HASH_C = "c" * 64
HASH_D = "d" * 64


def make_manifest_line(sha256, path):
    return f"{sha256}  {path}\n"


class TestMergeDepot(unittest.TestCase):
    """Tests for 'envy merge-depot' command."""

    def setUp(self):
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-merge-depot-test-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent

    def tearDown(self):
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def _write_manifest(self, name, lines):
        """Write a depot manifest file and return its path."""
        path = self.test_dir / name
        path.write_text("".join(lines), encoding="utf-8")
        return path

    def _run_merge(self, *args):
        """Run envy merge-depot and return CompletedProcess."""
        cmd = [str(self.envy), "merge-depot", *[str(a) for a in args]]
        return test_config.run(
            cmd,
            cwd=self.project_root,
            capture_output=True,
            text=True,
        )

    @staticmethod
    def _parse_output_line(line):
        """Parse a single '<hash>  <path>' line -> (hash, path_str)."""
        parts = line.strip().split("  ", 1)
        if len(parts) != 2:
            raise ValueError(f"Expected '<hash>  <path>', got: {line}")
        return parts[0], parts[1]

    def _parse_output(self, stdout):
        """Parse merge-depot stdout into list of (hash, path_str) tuples."""
        lines = [l for l in stdout.strip().split("\n") if l.strip()]
        return [self._parse_output_line(l) for l in lines]

    # --- Basic merge ---

    def test_single_manifest(self):
        """Merging a single manifest passes it through."""
        m = self._write_manifest("darwin.txt", [
            make_manifest_line(HASH_A, "pkg-darwin.tar.zst"),
        ])
        result = self._run_merge(m)
        self.assertEqual(result.returncode, 0, result.stderr)
        entries = self._parse_output(result.stdout)
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0][0], HASH_A)

    def test_merge_two_manifests(self):
        """Merging two manifests produces union of entries."""
        darwin = self._write_manifest("darwin.txt", [
            make_manifest_line(HASH_A, "pkg-darwin.tar.zst"),
        ])
        linux = self._write_manifest("linux.txt", [
            make_manifest_line(HASH_B, "pkg-linux.tar.zst"),
        ])
        result = self._run_merge(darwin, linux)
        self.assertEqual(result.returncode, 0, result.stderr)
        entries = self._parse_output(result.stdout)
        self.assertEqual(len(entries), 2)

    def test_output_sorted_by_path(self):
        """Output lines are sorted alphabetically by path."""
        m = self._write_manifest("unsorted.txt", [
            make_manifest_line(HASH_C, "zzz.tar.zst"),
            make_manifest_line(HASH_A, "aaa.tar.zst"),
            make_manifest_line(HASH_B, "mmm.tar.zst"),
        ])
        result = self._run_merge(m)
        self.assertEqual(result.returncode, 0, result.stderr)
        entries = self._parse_output(result.stdout)
        paths = [str(e[1]) for e in entries]
        self.assertEqual(paths, ["aaa.tar.zst", "mmm.tar.zst", "zzz.tar.zst"])

    def test_deduplicates_identical_entries(self):
        """Identical entries across manifests are deduplicated."""
        a = self._write_manifest("a.txt", [
            make_manifest_line(HASH_A, "same.tar.zst"),
        ])
        b = self._write_manifest("b.txt", [
            make_manifest_line(HASH_A, "same.tar.zst"),
        ])
        result = self._run_merge(a, b)
        self.assertEqual(result.returncode, 0, result.stderr)
        entries = self._parse_output(result.stdout)
        self.assertEqual(len(entries), 1)

    # --- --existing flag ---

    def test_existing_entries_preserved(self):
        """Existing entries not in new manifests are preserved."""
        existing = self._write_manifest("existing.txt", [
            make_manifest_line(HASH_C, "old-pkg.tar.zst"),
        ])
        new = self._write_manifest("new.txt", [
            make_manifest_line(HASH_A, "new-pkg.tar.zst"),
        ])
        result = self._run_merge(new, "--existing", existing)
        self.assertEqual(result.returncode, 0, result.stderr)
        entries = self._parse_output(result.stdout)
        self.assertEqual(len(entries), 2)
        paths = {str(e[1]) for e in entries}
        self.assertIn("old-pkg.tar.zst", paths)
        self.assertIn("new-pkg.tar.zst", paths)

    def test_existing_and_new_same_hash(self):
        """Entry in both existing and new with same hash is deduplicated."""
        existing = self._write_manifest("existing.txt", [
            make_manifest_line(HASH_A, "shared.tar.zst"),
        ])
        new = self._write_manifest("new.txt", [
            make_manifest_line(HASH_A, "shared.tar.zst"),
        ])
        result = self._run_merge(new, "--existing", existing)
        self.assertEqual(result.returncode, 0, result.stderr)
        entries = self._parse_output(result.stdout)
        self.assertEqual(len(entries), 1)

    # --- Hash conflicts ---

    def test_hash_change_vs_existing_warns(self):
        """Different hash vs existing emits warning, new hash wins."""
        existing = self._write_manifest("existing.txt", [
            make_manifest_line(HASH_A, "changed.tar.zst"),
        ])
        new = self._write_manifest("new.txt", [
            make_manifest_line(HASH_B, "changed.tar.zst"),
        ])
        result = self._run_merge(new, "--existing", existing)
        self.assertEqual(result.returncode, 0, result.stderr)
        entries = self._parse_output(result.stdout)
        self.assertEqual(len(entries), 1)
        # New hash should win
        self.assertEqual(entries[0][0], HASH_B)
        # Warning should appear on stderr
        self.assertIn("hash changed", result.stderr)

    def test_hash_change_vs_existing_strict_errors(self):
        """With --strict, different hash vs existing is a hard error."""
        existing = self._write_manifest("existing.txt", [
            make_manifest_line(HASH_A, "changed.tar.zst"),
        ])
        new = self._write_manifest("new.txt", [
            make_manifest_line(HASH_B, "changed.tar.zst"),
        ])
        result = self._run_merge(new, "--existing", existing, "--strict")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("hash changed", result.stderr)

    def test_cross_input_conflict_errors(self):
        """Same path with different hashes across new inputs is always an error."""
        a = self._write_manifest("a.txt", [
            make_manifest_line(HASH_A, "conflict.tar.zst"),
        ])
        b = self._write_manifest("b.txt", [
            make_manifest_line(HASH_B, "conflict.tar.zst"),
        ])
        result = self._run_merge(a, b)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("conflicting hashes", result.stderr)

    # --- Edge cases ---

    def test_no_arguments_fails(self):
        """merge-depot with no arguments fails."""
        result = self._run_merge()
        self.assertNotEqual(result.returncode, 0)

    def test_comments_and_blanks_skipped(self):
        """Comment and blank lines in manifests are ignored."""
        m = self._write_manifest("with_comments.txt", [
            "# header comment\n",
            "\n",
            make_manifest_line(HASH_A, "pkg.tar.zst"),
            "# trailing comment\n",
        ])
        result = self._run_merge(m)
        self.assertEqual(result.returncode, 0, result.stderr)
        entries = self._parse_output(result.stdout)
        self.assertEqual(len(entries), 1)

    def test_url_prefixed_paths(self):
        """Paths with URL prefixes (from --depot-prefix) are preserved."""
        m = self._write_manifest("urls.txt", [
            make_manifest_line(HASH_A, "https://cdn.example.com/pkg.tar.zst"),
        ])
        result = self._run_merge(m)
        self.assertEqual(result.returncode, 0, result.stderr)
        entries = self._parse_output(result.stdout)
        self.assertEqual(entries[0][1], "https://cdn.example.com/pkg.tar.zst")

    def test_large_merge_superset(self):
        """Merge preserves all entries from existing + adds new ones."""
        existing_lines = [
            make_manifest_line(f"{i:064x}", f"existing-{i}.tar.zst")
            for i in range(10)
        ]
        new_lines = [
            make_manifest_line(f"{i:064x}", f"new-{i}.tar.zst")
            for i in range(100, 105)
        ]
        existing = self._write_manifest("existing.txt", existing_lines)
        new = self._write_manifest("new.txt", new_lines)
        result = self._run_merge(new, "--existing", existing)
        self.assertEqual(result.returncode, 0, result.stderr)
        entries = self._parse_output(result.stdout)
        self.assertEqual(len(entries), 15)

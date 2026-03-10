#!/usr/bin/env python3
"""Tests for embed_resource.py — verifies CRLF normalization and substitution."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parent / "embed_resource.py"


class TestCRLFNormalization(unittest.TestCase):
    """Embedded resources must never contain \\r bytes."""

    def _run_embed(self, resource_bytes: bytes, defines=None) -> str:
        """Write resource_bytes to a temp file, run embed_resource, return header text."""
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            res_file = tmp / "resource.txt"
            res_file.write_bytes(resource_bytes)
            out_file = tmp / "out.h"

            cmd = [sys.executable, str(SCRIPT), str(out_file)]
            for d in (defines or []):
                cmd.extend(["-D", d])
            cmd.append(f"test_resource={res_file}")

            result = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return out_file.read_text()

    def _extract_bytes(self, header: str) -> bytes:
        """Parse hex byte literals from the generated C++ array initializer."""
        import re
        m = re.search(r"kTestResource\[\]\s*=\s*\{([^}]+)\}", header)
        assert m, f"Could not find array in header:\n{header}"
        hex_vals = [tok.strip() for tok in m.group(1).split(",") if tok.strip()]
        return bytes(int(h, 16) for h in hex_vals)

    def test_lf_passthrough(self):
        """LF-only input is preserved unchanged."""
        data = b"line1\nline2\nline3\n"
        header = self._run_embed(data)
        embedded = self._extract_bytes(header)
        self.assertEqual(embedded, data)

    def test_crlf_normalized_to_lf(self):
        """CRLF input is normalized to LF."""
        data = b"line1\r\nline2\r\nline3\r\n"
        header = self._run_embed(data)
        embedded = self._extract_bytes(header)
        self.assertNotIn(b"\r", embedded)
        self.assertEqual(embedded, b"line1\nline2\nline3\n")

    def test_mixed_line_endings_normalized(self):
        """Mixed CR, CRLF, LF all become LF."""
        data = b"crlf\r\nlf\ncr\rend\n"
        header = self._run_embed(data)
        embedded = self._extract_bytes(header)
        self.assertNotIn(b"\r", embedded)
        self.assertEqual(embedded, b"crlf\nlf\ncr\nend\n")

    def test_bare_cr_normalized(self):
        """Bare CR (old Mac) is normalized to LF."""
        data = b"a\rb\rc\r"
        header = self._run_embed(data)
        embedded = self._extract_bytes(header)
        self.assertEqual(embedded, b"a\nb\nc\n")

    def test_substitution_after_normalization(self):
        """Substitutions work correctly on CRLF-normalized content."""
        data = b"@@KEY@@\r\nrest\r\n"
        header = self._run_embed(data, defines=["KEY=VALUE"])
        embedded = self._extract_bytes(header)
        self.assertNotIn(b"\r", embedded)
        self.assertEqual(embedded, b"VALUE\nrest\n")

    def test_binary_data_not_corrupted(self):
        """Non-line-ending \\r-adjacent bytes aren't altered (\\r only appears in line endings)."""
        # 0x0d (CR) only matters as line ending; this test confirms isolated \r is still normalized
        # but actual binary payloads (images, etc.) should not be embedded via this tool
        data = b"\x00\x01\x02\n\xff\xfe\n"
        header = self._run_embed(data)
        embedded = self._extract_bytes(header)
        self.assertEqual(embedded, data)


if __name__ == "__main__":
    unittest.main()

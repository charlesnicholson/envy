"""Functional tests for 'envy cache' (location + disk usage report)."""

import shutil
import tempfile
import unittest
from pathlib import Path

from . import test_config


def parse_report(stdout: str) -> tuple[str, dict[str, list[tuple[str, str]]], str]:
    """Split the report into (cache path, {section: [(label, size)]}, total)."""
    root = ""
    total = ""
    sections: dict[str, list[tuple[str, str]]] = {}
    current: list[tuple[str, str]] | None = None

    for line in stdout.splitlines():
        if line.startswith("Cache: "):
            root = line[len("Cache: ") :]
        elif line.endswith(":") and not line.startswith(" "):
            current = sections.setdefault(line[:-1], [])
        elif line.startswith("  ") and line.strip() != "(none)":
            label, _, size = line.strip().rpartition("  ")
            label, size = label.strip(), size.strip()
            if label == "TOTAL":
                total = size
            elif current is not None:
                current.append((label, size))

    return root, sections, total


class TestCacheUsage(unittest.TestCase):
    """'envy cache' reports the cache location and per-entry sizes."""

    # Every invocation self-deploys the (sanitizer-instrumented) binary into the
    # scratch cache root, which is a large copy.
    envy_watchdog_timeout = 60

    def setUp(self):
        self.envy = test_config.get_envy_executable()
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-cache-usage-"))

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)

    def run_cache(self):
        result = test_config.run(
            [str(self.envy), "--cache-root", str(self.cache_root), "cache"],
            capture_output=True,
            text=True,
            env=test_config.get_test_env(),
        )
        self.assertEqual(result.returncode, 0, f"cache failed: {result.stderr}")
        return parse_report(result.stdout)

    def write_package(self, identity: str, key: str, size: int):
        entry = self.cache_root / "packages" / identity / key / "pkg"
        entry.mkdir(parents=True, exist_ok=True)
        (entry / "payload.bin").write_bytes(b"\0" * size)
        (entry.parent / "envy-complete").write_bytes(b"")

    def test_empty_cache_reports_location_and_no_packages(self):
        root, sections, total = self.run_cache()

        self.assertEqual(root, str(self.cache_root))
        self.assertEqual(sections["Packages"], [])
        # main() self-deploys before dispatch, so the running version is present.
        self.assertTrue(sections["Envy deployments"], "expected a deployed envy")
        self.assertTrue(total)

    def test_reports_each_package_and_deployment(self):
        self.write_package("pkg.big@1", "darwin-arm64-blake3-aaaa1111", 4096)
        self.write_package("pkg.small@2", "linux-x86_64-blake3-bbbb2222", 1024)

        _, sections, total = self.run_cache()

        packages = dict(sections["Packages"])
        self.assertEqual(packages["pkg.big@1/darwin-arm64-blake3-aaaa1111"], "4.00KB")
        self.assertEqual(packages["pkg.small@2/linux-x86_64-blake3-bbbb2222"], "1.00KB")

        # Largest first: the report exists to show what is worth reclaiming.
        labels = [label for label, _ in sections["Packages"]]
        self.assertEqual(labels[0], "pkg.big@1/darwin-arm64-blake3-aaaa1111")

        deployed = sections["Envy deployments"]
        self.assertEqual(len(deployed), 1, f"expected one deployment, got {deployed}")
        self.assertNotEqual(deployed[0][1], "0B")
        self.assertTrue(total)

    def test_nested_package_contents_are_summed(self):
        entry = self.cache_root / "packages" / "pkg.deep@1" / "darwin-arm64-blake3-cccc"
        deep = entry / "pkg" / "a" / "b" / "c"
        deep.mkdir(parents=True)
        (deep / "one.bin").write_bytes(b"\0" * 1024)
        (entry / "pkg" / "a" / "two.bin").write_bytes(b"\0" * 1024)

        _, sections, _ = self.run_cache()

        packages = dict(sections["Packages"])
        self.assertEqual(packages["pkg.deep@1/darwin-arm64-blake3-cccc"], "2.00KB")

    def test_non_package_directories_are_reported(self):
        specs = self.cache_root / "specs"
        specs.mkdir(parents=True)
        (specs / "some.spec@1.lua").write_bytes(b"\0" * 2048)

        _, sections, _ = self.run_cache()

        other = dict(sections["Other"])
        self.assertEqual(other["specs"], "2.00KB")


if __name__ == "__main__":
    unittest.main()

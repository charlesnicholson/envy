"""Functional tests for product command parallel execution."""

import hashlib
import io
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path

from . import test_config
from .test_config import make_manifest
from .trace_parser import TraceParser, PkgPhase

# Test archive contents
TEST_ARCHIVE_FILES = {
    "root/file1.txt": "Root file content\n",
    "root/file2.txt": "Another root file\n",
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


class TestProductParallelism(unittest.TestCase):
    """Test that product command extends all dependencies for parallel execution."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-product-parallel-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-product-manifest-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent

        # Create test archive and get its hash
        self.archive_path = self.test_dir / "test.tar.gz"
        self.archive_hash = create_test_archive(self.archive_path)

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def lua_path(self, path: Path) -> str:
        return path.as_posix()

    def manifest(self, content: str) -> Path:
        manifest_path = self.test_dir / "envy.lua"
        manifest_path.write_text(make_manifest(content), encoding="utf-8")
        return manifest_path

    def test_product_extends_full_dependency_closure_to_completion(self):
        """Product command must extend provider and all dependencies to completion.

        Regression test: product command should call extend_dependencies_to_completion()
        to set target_phase=completion for provider and all transitive dependencies
        BEFORE waiting for the provider to complete. This enables full parallelism
        instead of serial chain reaction where each spec waits for its dependencies.

        Test verifies via trace that:
        1. target_extended events for closure happen before thread execution
        2. Only specs in provider's closure have targets extended
        3. Unrelated specs remain at recipe_fetch
        """
        # Create dependency chain: tool_c (leaf) <- tool_b <- tool_a (provider)
        # Plus unrelated tool_d
        archive_lua_path = self.archive_path.as_posix()

        tool_c_spec = f"""IDENTITY = "local.tool_c@v1"
PRODUCTS = {{ tool_c = "bin/tool_c" }}
FETCH = {{
  source = "{archive_lua_path}",
  sha256 = "{self.archive_hash}"
}}
"""
        tool_c_path = self.test_dir / "tool_c.lua"
        tool_c_path.write_text(tool_c_spec, encoding="utf-8")

        tool_b_spec = f"""IDENTITY = "local.tool_b@v1"
PRODUCTS = {{ tool_b = "bin/tool_b" }}
DEPENDENCIES = {{
  {{
    spec = "local.tool_c@v1",
    source = "{self.lua_path(tool_c_path)}"
  }}
}}
FETCH = {{
  source = "{archive_lua_path}",
  sha256 = "{self.archive_hash}"
}}
"""
        tool_b_path = self.test_dir / "tool_b.lua"
        tool_b_path.write_text(tool_b_spec, encoding="utf-8")

        tool_a_spec = f"""IDENTITY = "local.tool_a@v1"
PRODUCTS = {{ tool_a = "bin/tool_a" }}
DEPENDENCIES = {{
  {{
    spec = "local.tool_b@v1",
    source = "{self.lua_path(tool_b_path)}"
  }}
}}
FETCH = {{
  source = "{archive_lua_path}",
  sha256 = "{self.archive_hash}"
}}
"""
        tool_a_path = self.test_dir / "tool_a.lua"
        tool_a_path.write_text(tool_a_spec, encoding="utf-8")

        # Unrelated spec (not in tool_a's dependency closure)
        tool_d_spec = f"""IDENTITY = "local.tool_d@v1"
PRODUCTS = {{ tool_d = "bin/tool_d" }}
FETCH = {{
  source = "{archive_lua_path}",
  sha256 = "{self.archive_hash}"
}}
"""
        tool_d_path = self.test_dir / "tool_d.lua"
        tool_d_path.write_text(tool_d_spec, encoding="utf-8")

        # Manifest with all recipes
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{ spec = "local.tool_a@v1", source = "{self.lua_path(tool_a_path)}" }},
  {{ spec = "local.tool_d@v1", source = "{self.lua_path(tool_d_path)}" }}
}}
"""
        )

        # Run product command with trace enabled
        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy),
                "--cache-root",
                str(self.cache_root),
                f"--trace=file:{trace_file}",
                "product",
                "tool_a",
                "--manifest",
                str(manifest),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertTrue(trace_file.exists(), "Trace file should be created")

        # Parse trace events
        parser = TraceParser(trace_file)
        events = parser.parse()

        # Find all target_extended events
        target_extended_events = [e for e in events if e.event == "target_extended"]

        # Extract specs that had targets extended to completion (phase 7)
        extended_to_completion = set()
        for event in target_extended_events:
            spec = event.raw.get("spec")
            new_target = event.raw.get(
                "new_target_num"
            )  # Field is new_target_num, not new_target_phase_num
            if spec and new_target == PkgPhase.COMPLETION:
                extended_to_completion.add(spec)

        # Verify tool_a, tool_b, tool_c had targets extended to completion
        expected_closure = {"local.tool_a@v1", "local.tool_b@v1", "local.tool_c@v1"}

        self.assertTrue(
            expected_closure.issubset(extended_to_completion),
            f"Expected all specs in closure to have targets extended: "
            f"expected {expected_closure}, got {extended_to_completion}",
        )

        # Verify tool_d did NOT have target extended to completion (not in closure)
        self.assertNotIn(
            "local.tool_d@v1",
            extended_to_completion,
            "Unrelated spec should not have target extended to completion",
        )


if __name__ == "__main__":
    unittest.main()

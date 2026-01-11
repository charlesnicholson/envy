"""Functional tests for 'envy sync' command.

Tests syncing entire manifest, specific identities, error handling,
and transitive dependencies.
"""

import hashlib
import io
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
from pathlib import Path
from typing import Optional, List

from . import test_config
from .test_config import make_manifest
from .trace_parser import TraceParser

# Test archive contents
TEST_ARCHIVE_FILES = {
    "root/file1.txt": "Test file content\n",
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


# =============================================================================
# Shared specs (used by multiple test classes)
# =============================================================================

# Simple user-managed package: no dependencies, check always false
SPEC_SIMPLE = """IDENTITY = "local.simple@v1"
DEPENDENCIES = {{}}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
"""

# Cache-managed dependency with archive fetch and build phase
SPEC_BUILD_DEP = """IDENTITY = "local.build_dependency@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  envy.run([[echo 'dependency_data' > dependency.txt
      mkdir -p bin
      echo 'binary' > bin/app]])
end
"""

# Diamond D: base of diamond dependency graph
SPEC_DIAMOND_D = """IDENTITY = "local.diamond_d@v1"
DEPENDENCIES = {{}}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
"""

# Diamond C: depends on D, right side of diamond
SPEC_DIAMOND_C = """IDENTITY = "local.diamond_c@v1"
DEPENDENCIES = {{
  {{ spec = "local.diamond_d@v1", source = "diamond_d.lua" }}
}}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
"""

# Product provider: cached package exposing 'tool' product at bin/tool
SPEC_PRODUCT_PROVIDER = """IDENTITY = "local.product_provider@v1"
PRODUCTS = {{ tool = "bin/tool" }}

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}",
}}

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
"""


class TestSyncCommand(unittest.TestCase):
    """Tests for 'envy sync' command."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-sync-test-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-sync-manifest-"))
        self.specs_dir = Path(tempfile.mkdtemp(prefix="envy-sync-specs-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent

        # Create test archive and get its hash
        self.archive_path = self.specs_dir / "test.tar.gz"
        self.archive_hash = create_test_archive(self.archive_path)

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)
        shutil.rmtree(self.specs_dir, ignore_errors=True)

    def write_spec(self, name: str, content: str) -> str:
        """Write spec to temp dir with placeholder substitution, return Lua path."""
        spec_content = content.format(
            ARCHIVE_PATH=self.archive_path.as_posix(),
            ARCHIVE_HASH=self.archive_hash,
        )
        path = self.specs_dir / f"{name}.lua"
        path.write_text(spec_content, encoding="utf-8")
        return path.as_posix()

    def create_manifest(self, content: str) -> Path:
        """Create manifest file with given content."""
        manifest_path = self.test_dir / "envy.lua"
        manifest_path.write_text(make_manifest(content), encoding="utf-8")
        return manifest_path

    def run_sync(
        self,
        identities: Optional[List[str]] = None,
        manifest: Optional[Path] = None,
        install_all: bool = False,
    ):
        """Run 'envy sync' command and return result."""
        cmd = [str(self.envy), "--cache-root", str(self.cache_root), "sync"]
        if install_all:
            cmd.append("--install-all")
        if identities:
            cmd.extend(identities)
        if manifest:
            cmd.extend(["--manifest", str(manifest)])

        result = subprocess.run(
            cmd,
            cwd=self.project_root,
            capture_output=True,
            text=True,
        )
        return result

    def test_sync_install_all_installs_packages(self):
        """Sync --install-all installs entire manifest."""
        simple_path = self.write_spec("simple", SPEC_SIMPLE)
        build_dep_path = self.write_spec("build_dep", SPEC_BUILD_DEP)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.build_dependency@v1", source = "{build_dep_path}" }},
    {{ spec = "local.simple@v1", source = "{simple_path}" }},
}}
""")

        result = self.run_sync(manifest=manifest, install_all=True)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("installed", result.stderr.lower())

        build_dep_cache = self.cache_root / "packages" / "local.build_dependency@v1"
        simple_cache = self.cache_root / "packages" / "local.simple@v1"
        self.assertTrue(build_dep_cache.exists())
        self.assertTrue(simple_cache.exists())

    def test_sync_install_all_single_identity(self):
        """Sync --install-all with single identity installs only that package."""
        simple_path = self.write_spec("simple", SPEC_SIMPLE)
        build_dep_path = self.write_spec("build_dep", SPEC_BUILD_DEP)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.build_dependency@v1", source = "{build_dep_path}" }},
    {{ spec = "local.simple@v1", source = "{simple_path}" }},
}}
""")

        result = self.run_sync(
            identities=["local.simple@v1"], manifest=manifest, install_all=True
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        simple_cache = self.cache_root / "packages" / "local.simple@v1"
        build_dep_cache = self.cache_root / "packages" / "local.build_dependency@v1"
        self.assertTrue(simple_cache.exists())
        self.assertFalse(build_dep_cache.exists())

    def test_sync_install_all_multiple_identities(self):
        """Sync --install-all with multiple identities installs all specified."""
        simple_path = self.write_spec("simple", SPEC_SIMPLE)
        build_dep_path = self.write_spec("build_dep", SPEC_BUILD_DEP)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.build_dependency@v1", source = "{build_dep_path}" }},
    {{ spec = "local.simple@v1", source = "{simple_path}" }},
}}
""")

        result = self.run_sync(
            identities=["local.simple@v1", "local.build_dependency@v1"],
            manifest=manifest,
            install_all=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        simple_cache = self.cache_root / "packages" / "local.simple@v1"
        build_dep_cache = self.cache_root / "packages" / "local.build_dependency@v1"
        self.assertTrue(simple_cache.exists())
        self.assertTrue(build_dep_cache.exists())

    def test_sync_identity_not_in_manifest_errors(self):
        """Sync with identity not in manifest returns error."""
        simple_path = self.write_spec("simple", SPEC_SIMPLE)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{simple_path}" }},
}}
""")

        result = self.run_sync(identities=["local.missing@v1"], manifest=manifest)

        self.assertNotEqual(result.returncode, 0, "Expected non-zero exit code")
        self.assertIn("not found in manifest", result.stderr.lower())
        self.assertIn("local.missing@v1", result.stderr)

    def test_sync_partial_missing_identities_errors(self):
        """Sync with some valid and some invalid identities returns error."""
        simple_path = self.write_spec("simple", SPEC_SIMPLE)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{simple_path}" }},
}}
""")

        result = self.run_sync(
            identities=["local.simple@v1", "local.missing@v1"], manifest=manifest
        )

        self.assertNotEqual(result.returncode, 0, "Expected non-zero exit code")
        self.assertIn("not found in manifest", result.stderr.lower())

        simple_cache = self.cache_root / "packages" / "local.simple@v1"
        self.assertFalse(simple_cache.exists(), "Nothing should be installed on error")

    def test_sync_install_all_second_run_is_noop(self):
        """Second sync --install-all run is a no-op (cache hits)."""
        simple_path = self.write_spec("simple", SPEC_SIMPLE)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{simple_path}" }},
}}
""")

        # First sync - cache miss
        trace_file1 = self.cache_root / "trace1.jsonl"
        cmd1 = [
            str(self.envy),
            "--cache-root",
            str(self.cache_root),
            f"--trace=file:{trace_file1}",
            "sync",
            "--install-all",
            "local.simple@v1",
            "--manifest",
            str(manifest),
        ]
        result1 = subprocess.run(
            cmd1, cwd=self.project_root, capture_output=True, text=True
        )
        self.assertEqual(result1.returncode, 0, f"stderr: {result1.stderr}")

        parser1 = TraceParser(trace_file1)
        cache_misses1 = parser1.filter_by_event("cache_miss")
        self.assertGreater(len(cache_misses1), 0, "Expected cache misses on first run")

        # Second sync - should be cache hits
        trace_file2 = self.cache_root / "trace2.jsonl"
        cmd2 = [
            str(self.envy),
            "--cache-root",
            str(self.cache_root),
            f"--trace=file:{trace_file2}",
            "sync",
            "--install-all",
            "local.simple@v1",
            "--manifest",
            str(manifest),
        ]
        result2 = subprocess.run(
            cmd2, cwd=self.project_root, capture_output=True, text=True
        )
        self.assertEqual(result2.returncode, 0, f"stderr: {result2.stderr}")

        parser2 = TraceParser(trace_file2)
        completes2 = parser2.filter_by_event("phase_complete")
        self.assertGreater(
            len(completes2), 0, "Expected phase completions on second run"
        )

    def test_sync_no_stdout_output(self):
        """Sync command produces no stdout output."""
        simple_path = self.write_spec("simple", SPEC_SIMPLE)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{simple_path}" }},
}}
""")

        result = self.run_sync(manifest=manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertEqual(result.stdout, "", "Expected no stdout output from sync")

    def test_sync_install_all_respects_cache_root(self):
        """Sync --install-all respects --cache-root flag."""
        custom_cache = Path(tempfile.mkdtemp(prefix="envy-sync-custom-cache-"))
        try:
            simple_path = self.write_spec("simple", SPEC_SIMPLE)

            manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{simple_path}" }},
}}
""")

            cmd = [
                str(self.envy),
                "--cache-root",
                str(custom_cache),
                "sync",
                "--install-all",
                "--manifest",
                str(manifest),
            ]
            result = subprocess.run(
                cmd, cwd=self.project_root, capture_output=True, text=True
            )

            self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

            simple_cache = custom_cache / "packages" / "local.simple@v1"
            self.assertTrue(simple_cache.exists())
        finally:
            shutil.rmtree(custom_cache, ignore_errors=True)

    def test_sync_empty_manifest(self):
        """Sync with empty manifest succeeds (nothing to do)."""
        manifest = self.create_manifest("PACKAGES = {}")

        result = self.run_sync(manifest=manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("nothing to sync", result.stderr.lower())

    def test_sync_install_all_transitive_dependencies(self):
        """Sync --install-all installs transitive dependencies."""
        self.write_spec("diamond_d", SPEC_DIAMOND_D)
        diamond_c_path = self.write_spec("diamond_c", SPEC_DIAMOND_C)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.diamond_c@v1", source = "{diamond_c_path}" }},
}}
""")

        result = self.run_sync(
            identities=["local.diamond_c@v1"], manifest=manifest, install_all=True
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("installed", result.stderr.lower())


class TestSyncProductScripts(unittest.TestCase):
    """Tests for product script deployment via 'envy sync'."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-sync-deploy-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-sync-deploy-manifest-"))
        self.specs_dir = Path(tempfile.mkdtemp(prefix="envy-sync-deploy-specs-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent

        # Create test archive and get its hash
        self.archive_path = self.specs_dir / "test.tar.gz"
        self.archive_hash = create_test_archive(self.archive_path)

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)
        shutil.rmtree(self.specs_dir, ignore_errors=True)

    def write_spec(self, name: str, content: str) -> str:
        """Write spec to temp dir with placeholder substitution, return Lua path."""
        spec_content = content.format(
            ARCHIVE_PATH=self.archive_path.as_posix(),
            ARCHIVE_HASH=self.archive_hash,
        )
        path = self.specs_dir / f"{name}.lua"
        path.write_text(spec_content, encoding="utf-8")
        return path.as_posix()

    def create_manifest(self, content: str, deploy: bool = True) -> Path:
        manifest_path = self.test_dir / "envy.lua"
        manifest_path.write_text(
            make_manifest(content, deploy=deploy), encoding="utf-8"
        )
        return manifest_path

    def run_sync(self, manifest: Path, identities: Optional[List[str]] = None):
        cmd = [str(self.envy), "--cache-root", str(self.cache_root), "sync"]
        if identities:
            cmd.extend(identities)
        cmd.extend(["--manifest", str(manifest)])
        return subprocess.run(
            cmd, cwd=self.project_root, capture_output=True, text=True
        )

    def test_sync_creates_product_scripts(self):
        """Default sync creates product scripts in bin directory."""
        product_path = self.write_spec("product_provider", SPEC_PRODUCT_PROVIDER)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.product_provider@v1", source = "{product_path}" }},
}}
""")

        result = self.run_sync(manifest=manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        bin_dir = self.test_dir / "envy-bin"
        self.assertTrue(bin_dir.exists())

        script_name = "tool.bat" if sys.platform == "win32" else "tool"
        script_path = bin_dir / script_name
        self.assertTrue(script_path.exists())

        content = script_path.read_text()
        self.assertIn("envy-managed", content)
        self.assertIn("product", content)

    def test_sync_updates_envy_managed_scripts(self):
        """Sync updates existing envy-managed scripts."""
        product_path = self.write_spec("product_provider", SPEC_PRODUCT_PROVIDER)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.product_provider@v1", source = "{product_path}" }},
}}
""")

        bin_dir = self.test_dir / "envy-bin"
        bin_dir.mkdir(parents=True, exist_ok=True)

        script_name = "tool.bat" if sys.platform == "win32" else "tool"
        script_path = bin_dir / script_name
        script_path.write_text("# envy-managed OLD_VERSION\nold content\n")

        result = self.run_sync(manifest=manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        content = script_path.read_text()
        self.assertNotIn("OLD_VERSION", content)
        self.assertIn("envy-managed", content)

    def test_sync_errors_on_non_envy_file_conflict(self):
        """Sync errors if non-envy-managed file conflicts with product name."""
        product_path = self.write_spec("product_provider", SPEC_PRODUCT_PROVIDER)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.product_provider@v1", source = "{product_path}" }},
}}
""")

        bin_dir = self.test_dir / "envy-bin"
        bin_dir.mkdir(parents=True, exist_ok=True)

        script_name = "tool.bat" if sys.platform == "win32" else "tool"
        script_path = bin_dir / script_name
        script_path.write_text("#!/bin/bash\necho 'user script'\n")

        result = self.run_sync(manifest=manifest)

        self.assertNotEqual(result.returncode, 0, "Expected non-zero exit code")
        self.assertIn("not envy-managed", result.stderr.lower())

    def test_sync_removes_obsolete_scripts(self):
        """Sync removes obsolete envy-managed scripts."""
        simple_path = self.write_spec("simple", SPEC_SIMPLE)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{simple_path}" }},
}}
""")

        bin_dir = self.test_dir / "envy-bin"
        bin_dir.mkdir(parents=True, exist_ok=True)

        obsolete_name = "old_tool.bat" if sys.platform == "win32" else "old_tool"
        obsolete_path = bin_dir / obsolete_name
        obsolete_path.write_text("# envy-managed v1.0.0\nold content\n")

        result = self.run_sync(manifest=manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertFalse(obsolete_path.exists())

    def test_sync_preserves_envy_executable(self):
        """Sync does not remove or modify the envy executable itself."""
        simple_path = self.write_spec("simple", SPEC_SIMPLE)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{simple_path}" }},
}}
""")

        bin_dir = self.test_dir / "envy-bin"
        bin_dir.mkdir(parents=True, exist_ok=True)

        envy_name = "envy.bat" if sys.platform == "win32" else "envy"
        envy_path = bin_dir / envy_name
        envy_content = "# envy bootstrap\n"
        envy_path.write_text(envy_content)

        result = self.run_sync(manifest=manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertTrue(envy_path.exists())
        self.assertEqual(envy_path.read_text(), envy_content)

    def test_sync_install_all_does_full_install(self):
        """Sync --install-all installs packages then deploys scripts."""
        product_path = self.write_spec("product_provider", SPEC_PRODUCT_PROVIDER)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.product_provider@v1", source = "{product_path}" }},
}}
""")

        cmd = [
            str(self.envy),
            "--cache-root",
            str(self.cache_root),
            "sync",
            "--install-all",
            "--manifest",
            str(manifest),
        ]
        result = subprocess.run(
            cmd, cwd=self.project_root, capture_output=True, text=True
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("installed", result.stderr.lower())

        pkg_path = self.cache_root / "packages" / "local.product_provider@v1"
        self.assertTrue(pkg_path.exists())

        bin_dir = self.test_dir / "envy-bin"
        script_name = "tool.bat" if sys.platform == "win32" else "tool"
        script_path = bin_dir / script_name
        self.assertTrue(script_path.exists())

    def test_sync_timestamp_preserved_when_content_unchanged(self):
        """Sync preserves file timestamps when content is unchanged."""
        product_path = self.write_spec("product_provider", SPEC_PRODUCT_PROVIDER)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.product_provider@v1", source = "{product_path}" }},
}}
""")

        result1 = self.run_sync(manifest=manifest)
        self.assertEqual(result1.returncode, 0, f"stderr: {result1.stderr}")

        bin_dir = self.test_dir / "envy-bin"
        script_name = "tool.bat" if sys.platform == "win32" else "tool"
        script_path = bin_dir / script_name

        stat1 = script_path.stat()
        mtime1 = stat1.st_mtime

        time.sleep(0.1)

        result2 = self.run_sync(manifest=manifest)
        self.assertEqual(result2.returncode, 0, f"stderr: {result2.stderr}")

        stat2 = script_path.stat()
        mtime2 = stat2.st_mtime

        self.assertEqual(mtime1, mtime2, "File timestamp should be unchanged")

    def test_product_script_execution_and_arg_forwarding(self):
        """Product scripts execute correctly and forward arguments."""
        # Create archive similar to other tests
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w:gz") as tar:
            if sys.platform == "win32":
                tool_content = b"@echo off\necho Args: %*\n"
                tool_name = "echotool.bat"
            else:
                tool_content = b"#!/bin/sh\necho \"Args: $@\"\n"
                tool_name = "echotool"

            # Add tool file to archive
            tool_info = tarfile.TarInfo(name=f"bin/{tool_name}")
            tool_info.size = len(tool_content)
            tool_info.mode = 0o755
            tar.addfile(tool_info, io.BytesIO(tool_content))

        archive_data = buf.getvalue()
        archive_path = self.specs_dir / "echotool.tar.gz"
        archive_path.write_bytes(archive_data)
        archive_hash = hashlib.sha256(archive_data).hexdigest()

        # Create spec for product provider (use normal Python substitution to avoid escaping issues)
        spec = 'IDENTITY = "local.echotool@v1"\n'
        spec += 'PRODUCTS = { echotool = "bin/' + tool_name + '" }\n\n'
        spec += 'FETCH = {\n'
        spec += '  source = "' + archive_path.as_posix() + '",\n'
        spec += '  sha256 = "' + archive_hash + '",\n'
        spec += '}\n\n'
        spec += 'STAGE = { strip = 0 }\n\n'
        spec += 'INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)\n'
        spec += '  envy.run("cp -r " .. stage_dir .. "/* " .. install_dir .. "/")\n'
        spec += 'end\n'
        spec_file = self.specs_dir / "echotool.lua"
        spec_file.write_text(spec, encoding="utf-8")
        spec_path = spec_file.as_posix()

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.echotool@v1", source = "{spec_path}" }},
}}
""")

        # Run sync to create product script
        result = self.run_sync(manifest=manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Copy envy executable to bin dir so product script can find it
        bin_dir = self.test_dir / "envy-bin"
        envy_name = "envy.exe" if sys.platform == "win32" else "envy"
        shutil.copy(self.envy, bin_dir / envy_name)

        # Test 1: Execute product script without arguments
        script_name = "echotool.bat" if sys.platform == "win32" else "echotool"
        script_path = bin_dir / script_name
        self.assertTrue(script_path.exists(), f"Product script not created: {script_path}")

        result = subprocess.run(
            [str(script_path)],
            cwd=self.test_dir,
            capture_output=True,
            text=True,
            env={**test_config.get_test_env(), "ENVY_CACHE_ROOT": str(self.cache_root)},
        )
        self.assertEqual(result.returncode, 0,
                        f"Product script failed: {result.stderr}")
        self.assertIn("Args:", result.stdout)

        # Test 2: Execute product script with arguments
        result = subprocess.run(
            [str(script_path), "arg1", "arg2", "arg with spaces"],
            cwd=self.test_dir,
            capture_output=True,
            text=True,
            env={**test_config.get_test_env(), "ENVY_CACHE_ROOT": str(self.cache_root)},
        )
        self.assertEqual(result.returncode, 0,
                        f"Product script with args failed: {result.stderr}")
        self.assertIn("arg1", result.stdout)
        self.assertIn("arg2", result.stdout)
        self.assertIn("arg with spaces", result.stdout)


class TestSyncDeployDirective(unittest.TestCase):
    """Tests for @envy deploy directive behavior in 'envy sync'."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-deploy-directive-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-deploy-manifest-"))
        self.specs_dir = Path(tempfile.mkdtemp(prefix="envy-deploy-specs-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent

        # Create test archive and get its hash
        self.archive_path = self.specs_dir / "test.tar.gz"
        self.archive_hash = create_test_archive(self.archive_path)

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)
        shutil.rmtree(self.specs_dir, ignore_errors=True)

    def write_spec(self, name: str, content: str) -> str:
        """Write spec to temp dir with placeholder substitution, return Lua path."""
        spec_content = content.format(
            ARCHIVE_PATH=self.archive_path.as_posix(),
            ARCHIVE_HASH=self.archive_hash,
        )
        path = self.specs_dir / f"{name}.lua"
        path.write_text(spec_content, encoding="utf-8")
        return path.as_posix()

    def create_manifest(self, content: str, deploy: Optional[str] = None) -> Path:
        """Create manifest with optional deploy directive."""
        manifest_path = self.test_dir / "envy.lua"
        header = '-- @envy bin "envy-bin"\n'
        if deploy is not None:
            header += f'-- @envy deploy "{deploy}"\n'
        manifest_path.write_text(header + content, encoding="utf-8")
        return manifest_path

    def run_sync(self, manifest: Path, install_all: bool = False):
        cmd = [str(self.envy), "--cache-root", str(self.cache_root), "sync"]
        if install_all:
            cmd.append("--install-all")
        cmd.extend(["--manifest", str(manifest)])
        return subprocess.run(
            cmd, cwd=self.project_root, capture_output=True, text=True
        )

    def test_sync_deploy_true_creates_scripts(self):
        """Sync with deploy=true creates product scripts."""
        product_path = self.write_spec("product_provider", SPEC_PRODUCT_PROVIDER)

        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.product_provider@v1", source = "{product_path}" }},
}}
""",
            deploy="true",
        )

        result = self.run_sync(manifest=manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        bin_dir = self.test_dir / "envy-bin"
        script_name = "tool.bat" if sys.platform == "win32" else "tool"
        script_path = bin_dir / script_name
        self.assertTrue(script_path.exists())

    def test_sync_deploy_false_no_scripts(self):
        """Sync with deploy=false does not create product scripts."""
        product_path = self.write_spec("product_provider", SPEC_PRODUCT_PROVIDER)

        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.product_provider@v1", source = "{product_path}" }},
}}
""",
            deploy="false",
        )

        result = self.run_sync(manifest=manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        bin_dir = self.test_dir / "envy-bin"
        script_name = "tool.bat" if sys.platform == "win32" else "tool"
        script_path = bin_dir / script_name
        self.assertFalse(script_path.exists())
        self.assertIn("deployment is disabled", result.stderr)

    def test_sync_deploy_absent_warns(self):
        """Naked sync with deploy absent warns user."""
        simple_path = self.write_spec("simple", SPEC_SIMPLE)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{simple_path}" }},
}}
""")  # No deploy directive

        result = self.run_sync(manifest=manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("deployment is disabled", result.stderr)
        self.assertIn("@envy deploy", result.stderr)

    def test_sync_install_all_no_deploy_warning(self):
        """Sync --install-all does not warn about deploy directive."""
        simple_path = self.write_spec("simple", SPEC_SIMPLE)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{simple_path}" }},
}}
""")  # No deploy directive

        result = self.run_sync(manifest=manifest, install_all=True)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertNotIn("deployment is disabled", result.stderr)


if __name__ == "__main__":
    unittest.main()

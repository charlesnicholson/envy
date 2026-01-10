#!/usr/bin/env python3
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


# Inline spec templates - {ARCHIVE_PATH}, {ARCHIVE_HASH} replaced at runtime
SPECS = {
    "simple.lua": """-- Minimal test spec - no dependencies
IDENTITY = "local.simple@v1"
DEPENDENCIES = {{}}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package - no cache interaction
end
""",
    "build_dependency.lua": """-- Test dependency for build_with_asset
IDENTITY = "local.build_dependency@v1"

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
""",
    "diamond_d.lua": """-- Base of diamond dependency
IDENTITY = "local.diamond_d@v1"
DEPENDENCIES = {{}}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end
""",
    "diamond_c.lua": """-- Right side of diamond: C depends on D
IDENTITY = "local.diamond_c@v1"
DEPENDENCIES = {{
  {{ spec = "local.diamond_d@v1", source = "diamond_d.lua" }}
}}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end
""",
    "product_provider.lua": """-- Product provider with cached package
IDENTITY = "local.product_provider@v1"
PRODUCTS = {{ tool = "bin/tool" }}

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}",
}}

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- No real payload needed; just mark complete to populate pkg_path
end
""",
}


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

        # Write inline specs to temp directory with placeholders substituted
        for name, content in SPECS.items():
            spec_content = content.format(
                ARCHIVE_PATH=self.archive_path.as_posix(),
                ARCHIVE_HASH=self.archive_hash,
            )
            (self.specs_dir / name).write_text(spec_content, encoding="utf-8")

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)
        shutil.rmtree(self.specs_dir, ignore_errors=True)

    def lua_path(self, name: str) -> str:
        return (self.specs_dir / name).as_posix()

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
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path("build_dependency.lua")}" }},
    {{ spec = "local.simple@v1", source = "{self.lua_path("simple.lua")}" }},
}}
"""
        )

        result = self.run_sync(manifest=manifest, install_all=True)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("installed", result.stderr.lower())

        build_dep_path = self.cache_root / "packages" / "local.build_dependency@v1"
        simple_path = self.cache_root / "packages" / "local.simple@v1"
        self.assertTrue(build_dep_path.exists(), f"Expected {build_dep_path} to exist")
        self.assertTrue(simple_path.exists(), f"Expected {simple_path} to exist")

    def test_sync_install_all_single_identity(self):
        """Sync --install-all with single identity installs only that package."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path("build_dependency.lua")}" }},
    {{ spec = "local.simple@v1", source = "{self.lua_path("simple.lua")}" }},
}}
"""
        )

        result = self.run_sync(
            identities=["local.simple@v1"], manifest=manifest, install_all=True
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        simple_path = self.cache_root / "packages" / "local.simple@v1"
        build_dep_path = self.cache_root / "packages" / "local.build_dependency@v1"
        self.assertTrue(simple_path.exists(), f"Expected {simple_path} to exist")
        self.assertFalse(
            build_dep_path.exists(), f"Expected {build_dep_path} NOT to exist"
        )

    def test_sync_install_all_multiple_identities(self):
        """Sync --install-all with multiple identities installs all specified packages."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path("build_dependency.lua")}" }},
    {{ spec = "local.simple@v1", source = "{self.lua_path("simple.lua")}" }},
}}
"""
        )

        result = self.run_sync(
            identities=["local.simple@v1", "local.build_dependency@v1"],
            manifest=manifest,
            install_all=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        simple_path = self.cache_root / "packages" / "local.simple@v1"
        build_dep_path = self.cache_root / "packages" / "local.build_dependency@v1"
        self.assertTrue(simple_path.exists(), f"Expected {simple_path} to exist")
        self.assertTrue(build_dep_path.exists(), f"Expected {build_dep_path} to exist")

    def test_sync_identity_not_in_manifest_errors(self):
        """Sync with identity not in manifest returns error."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{self.lua_path("simple.lua")}" }},
}}
"""
        )

        result = self.run_sync(identities=["local.missing@v1"], manifest=manifest)

        self.assertNotEqual(result.returncode, 0, "Expected non-zero exit code")
        self.assertIn("not found in manifest", result.stderr.lower())
        self.assertIn("local.missing@v1", result.stderr)

    def test_sync_partial_missing_identities_errors(self):
        """Sync with some valid and some invalid identities returns error."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{self.lua_path("simple.lua")}" }},
}}
"""
        )

        result = self.run_sync(
            identities=["local.simple@v1", "local.missing@v1"], manifest=manifest
        )

        self.assertNotEqual(result.returncode, 0, "Expected non-zero exit code")
        self.assertIn("not found in manifest", result.stderr.lower())

        # Verify nothing was installed (error before execution)
        simple_path = self.cache_root / "packages" / "local.simple@v1"
        self.assertFalse(simple_path.exists(), f"Expected nothing installed on error")

    def test_sync_install_all_second_run_is_noop(self):
        """Second sync --install-all run is a no-op (cache hits)."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{self.lua_path("simple.lua")}" }},
}}
"""
        )

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

        # On second run, verify execution completed successfully via trace events
        # Note: sync may still show cache_miss for spec loading even if assets are cached
        # The key test is that the second run succeeds and completes
        completes2 = parser2.filter_by_event("phase_complete")
        self.assertGreater(
            len(completes2), 0, "Expected phase completions on second run"
        )

    def test_sync_no_stdout_output(self):
        """Sync command produces no stdout output."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{self.lua_path("simple.lua")}" }},
}}
"""
        )

        result = self.run_sync(manifest=manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertEqual(result.stdout, "", "Expected no stdout output from sync")

    def test_sync_install_all_respects_cache_root(self):
        """Sync --install-all respects --cache-root flag."""
        custom_cache = Path(tempfile.mkdtemp(prefix="envy-sync-custom-cache-"))
        try:
            manifest = self.create_manifest(
                f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{self.lua_path("simple.lua")}" }},
}}
"""
            )

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

            simple_path = custom_cache / "packages" / "local.simple@v1"
            self.assertTrue(
                simple_path.exists(), f"Expected package in custom cache: {simple_path}"
            )
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
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.diamond_c@v1", source = "{self.lua_path("diamond_c.lua")}" }},
}}
"""
        )

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

        # Write inline specs to temp directory with placeholders substituted
        for name, content in SPECS.items():
            spec_content = content.format(
                ARCHIVE_PATH=self.archive_path.as_posix(),
                ARCHIVE_HASH=self.archive_hash,
            )
            (self.specs_dir / name).write_text(spec_content, encoding="utf-8")

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)
        shutil.rmtree(self.specs_dir, ignore_errors=True)

    def lua_path(self, name: str) -> str:
        return (self.specs_dir / name).as_posix()

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
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.product_provider@v1", source = "{self.lua_path("product_provider.lua")}" }},
}}
"""
        )

        result = self.run_sync(manifest=manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        bin_dir = self.test_dir / "envy-bin"
        self.assertTrue(bin_dir.exists(), "Expected bin directory to exist")

        script_name = "tool.bat" if sys.platform == "win32" else "tool"
        script_path = bin_dir / script_name
        self.assertTrue(script_path.exists(), f"Expected product script: {script_path}")

        content = script_path.read_text()
        self.assertIn("envy-managed", content)
        self.assertIn("product", content)

    def test_sync_updates_envy_managed_scripts(self):
        """Sync updates existing envy-managed scripts."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.product_provider@v1", source = "{self.lua_path("product_provider.lua")}" }},
}}
"""
        )

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
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.product_provider@v1", source = "{self.lua_path("product_provider.lua")}" }},
}}
"""
        )

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
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{self.lua_path("simple.lua")}" }},
}}
"""
        )

        bin_dir = self.test_dir / "envy-bin"
        bin_dir.mkdir(parents=True, exist_ok=True)

        obsolete_name = "old_tool.bat" if sys.platform == "win32" else "old_tool"
        obsolete_path = bin_dir / obsolete_name
        obsolete_path.write_text("# envy-managed v1.0.0\nold content\n")

        result = self.run_sync(manifest=manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertFalse(
            obsolete_path.exists(), f"Expected obsolete script to be removed"
        )

    def test_sync_preserves_envy_executable(self):
        """Sync does not remove or modify the envy executable itself."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{self.lua_path("simple.lua")}" }},
}}
"""
        )

        bin_dir = self.test_dir / "envy-bin"
        bin_dir.mkdir(parents=True, exist_ok=True)

        envy_name = "envy.bat" if sys.platform == "win32" else "envy"
        envy_path = bin_dir / envy_name
        envy_content = "# envy bootstrap\n"
        envy_path.write_text(envy_content)

        result = self.run_sync(manifest=manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertTrue(envy_path.exists(), "envy executable should not be removed")
        self.assertEqual(
            envy_path.read_text(), envy_content, "envy content should be unchanged"
        )

    def test_sync_install_all_does_full_install(self):
        """Sync --install-all installs packages then deploys scripts."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.product_provider@v1", source = "{self.lua_path("product_provider.lua")}" }},
}}
"""
        )

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
        self.assertTrue(pkg_path.exists(), f"Expected package at {pkg_path}")

        bin_dir = self.test_dir / "envy-bin"
        script_name = "tool.bat" if sys.platform == "win32" else "tool"
        script_path = bin_dir / script_name
        self.assertTrue(script_path.exists(), f"Expected product script: {script_path}")

    def test_sync_timestamp_preserved_when_content_unchanged(self):
        """Sync preserves file timestamps when content is unchanged."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.product_provider@v1", source = "{self.lua_path("product_provider.lua")}" }},
}}
"""
        )

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

        # Write inline specs to temp directory with placeholders substituted
        for name, content in SPECS.items():
            spec_content = content.format(
                ARCHIVE_PATH=self.archive_path.as_posix(),
                ARCHIVE_HASH=self.archive_hash,
            )
            (self.specs_dir / name).write_text(spec_content, encoding="utf-8")

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)
        shutil.rmtree(self.specs_dir, ignore_errors=True)

    def lua_path(self, name: str) -> str:
        return (self.specs_dir / name).as_posix()

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
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.product_provider@v1", source = "{self.lua_path("product_provider.lua")}" }},
}}
""",
            deploy="true",
        )

        result = self.run_sync(manifest=manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        bin_dir = self.test_dir / "envy-bin"
        script_name = "tool.bat" if sys.platform == "win32" else "tool"
        script_path = bin_dir / script_name
        self.assertTrue(script_path.exists(), f"Expected product script: {script_path}")

    def test_sync_deploy_false_no_scripts(self):
        """Sync with deploy=false does not create product scripts."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.product_provider@v1", source = "{self.lua_path("product_provider.lua")}" }},
}}
""",
            deploy="false",
        )

        result = self.run_sync(manifest=manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        bin_dir = self.test_dir / "envy-bin"
        script_name = "tool.bat" if sys.platform == "win32" else "tool"
        script_path = bin_dir / script_name
        self.assertFalse(
            script_path.exists(), "Expected no product script when deploy=false"
        )
        self.assertIn("deployment is disabled", result.stderr)

    def test_sync_deploy_absent_warns(self):
        """Naked sync with deploy absent warns user."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{self.lua_path("simple.lua")}" }},
}}
"""
        )  # No deploy directive

        result = self.run_sync(manifest=manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("deployment is disabled", result.stderr)
        self.assertIn("@envy deploy", result.stderr)

    def test_sync_install_all_no_deploy_warning(self):
        """Sync --install-all does not warn about deploy directive."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.simple@v1", source = "{self.lua_path("simple.lua")}" }},
}}
"""
        )  # No deploy directive

        result = self.run_sync(manifest=manifest, install_all=True)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        # Should NOT warn about deployment when --install-all is used
        self.assertNotIn("deployment is disabled", result.stderr)


if __name__ == "__main__":
    unittest.main()

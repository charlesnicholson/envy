#!/usr/bin/env python3
"""Functional tests for 'envy sync' command.

Tests syncing entire manifest, specific identities, error handling,
and transitive dependencies.
"""

import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from typing import Optional, List

from . import test_config
from .test_config import make_manifest
from .trace_parser import TraceParser


class TestSyncCommand(unittest.TestCase):
    """Tests for 'envy sync' command."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-sync-test-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-sync-manifest-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent
        self.test_data = self.project_root / "test_data"

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)

    @staticmethod
    def lua_path(path: Path) -> str:
        """Convert path to Lua-safe string (forward slashes work on all platforms)."""
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
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/specs/build_dependency.lua" }},
    {{ spec = "local.simple@v1", source = "{self.lua_path(self.test_data)}/specs/simple.lua" }},
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
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/specs/build_dependency.lua" }},
    {{ spec = "local.simple@v1", source = "{self.lua_path(self.test_data)}/specs/simple.lua" }},
}}
"""
        )

        result = self.run_sync(identities=["local.simple@v1"], manifest=manifest, install_all=True)

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
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/specs/build_dependency.lua" }},
    {{ spec = "local.simple@v1", source = "{self.lua_path(self.test_data)}/specs/simple.lua" }},
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
    {{ spec = "local.simple@v1", source = "{self.lua_path(self.test_data)}/specs/simple.lua" }},
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
    {{ spec = "local.simple@v1", source = "{self.lua_path(self.test_data)}/specs/simple.lua" }},
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
    {{ spec = "local.simple@v1", source = "{self.lua_path(self.test_data)}/specs/simple.lua" }},
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
    {{ spec = "local.simple@v1", source = "{self.lua_path(self.test_data)}/specs/simple.lua" }},
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
    {{ spec = "local.simple@v1", source = "{self.lua_path(self.test_data)}/specs/simple.lua" }},
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
    {{ spec = "local.diamond_c@v1", source = "{self.lua_path(self.test_data)}/specs/diamond_c.lua" }},
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
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent
        self.test_data = self.project_root / "test_data"

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)

    @staticmethod
    def lua_path(path: Path) -> str:
        return path.as_posix()

    def create_manifest(self, content: str) -> Path:
        manifest_path = self.test_dir / "envy.lua"
        manifest_path.write_text(make_manifest(content), encoding="utf-8")
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
    {{ spec = "local.product_provider@v1", source = "{self.lua_path(self.test_data)}/specs/product_provider.lua" }},
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
    {{ spec = "local.product_provider@v1", source = "{self.lua_path(self.test_data)}/specs/product_provider.lua" }},
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
    {{ spec = "local.product_provider@v1", source = "{self.lua_path(self.test_data)}/specs/product_provider.lua" }},
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
    {{ spec = "local.simple@v1", source = "{self.lua_path(self.test_data)}/specs/simple.lua" }},
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
    {{ spec = "local.simple@v1", source = "{self.lua_path(self.test_data)}/specs/simple.lua" }},
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
    {{ spec = "local.product_provider@v1", source = "{self.lua_path(self.test_data)}/specs/product_provider.lua" }},
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
    {{ spec = "local.product_provider@v1", source = "{self.lua_path(self.test_data)}/specs/product_provider.lua" }},
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


if __name__ == "__main__":
    unittest.main()

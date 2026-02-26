"""Functional tests for 'envy export' and 'envy import' commands."""

import hashlib
import io
import os
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path

from . import test_config
from .test_config import make_manifest

TEST_ARCHIVE_FILES = {
    "root/file1.txt": "Root file content\n",
    "root/file2.txt": "Another root file\n",
    "root/subdir1/file3.txt": "Subdirectory file\n",
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


class TestExportImport(unittest.TestCase):
    """Tests for 'envy export' and 'envy import' commands."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-export-test-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-export-manifest-"))
        self.output_dir = Path(tempfile.mkdtemp(prefix="envy-export-output-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent

        # Create test archive
        self.archive_path = self.test_dir / "test.tar.gz"
        self.archive_hash = create_test_archive(self.archive_path)
        self._write_specs()

    def _write_specs(self):
        archive_lua_path = self.archive_path.as_posix()

        specs = {
            "exportable_pkg.lua": f'''IDENTITY = "local.exportable_pkg@v1"
EXPORTABLE = true

FETCH = {{
  source = "{archive_lua_path}",
  sha256 = "{self.archive_hash}"
}}

STAGE = {{strip = 1}}

BUILD = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  if envy.PLATFORM == "windows" then
    envy.run([[Set-Content -Path built.txt -Value "built"]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[echo 'built' > built.txt]])
  end
end
''',
            "default_pkg.lua": f'''IDENTITY = "local.default_pkg@v1"

FETCH = {{
  source = "{archive_lua_path}",
  sha256 = "{self.archive_hash}"
}}

STAGE = {{strip = 1}}

BUILD = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  if envy.PLATFORM == "windows" then
    envy.run([[Set-Content -Path built.txt -Value "built"]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[echo 'built' > built.txt]])
  end
end
''',
        }

        for name, content in specs.items():
            (self.test_dir / name).write_text(content, encoding="utf-8")

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)
        shutil.rmtree(self.output_dir, ignore_errors=True)

    @staticmethod
    def lua_path(path: Path) -> str:
        return path.as_posix()

    def create_manifest(self, content: str) -> Path:
        manifest_path = self.test_dir / "envy.lua"
        manifest_path.write_text(make_manifest(content), encoding="utf-8")
        return manifest_path

    def run_envy(self, *args, cwd=None):
        cmd = [str(self.envy), "--cache-root", str(self.cache_root), *args]
        return test_config.run(
            cmd,
            cwd=cwd or self.project_root,
            capture_output=True,
            text=True,
        )

    def _install_and_export(self, spec_name, identity, manifest):
        """Install a package and export it. Returns (install_result, export_result)."""
        install = self.run_envy("install", "--manifest", str(manifest))
        self.assertEqual(install.returncode, 0, f"install failed: {install.stderr}")

        export = self.run_envy(
            "export", identity,
            "-o", str(self.output_dir),
            "--manifest", str(manifest),
        )
        return install, export

    def test_export_single_package(self):
        """Export a single EXPORTABLE=true package, verify archive created."""
        manifest = self.create_manifest(
            f'''
PACKAGES = {{
    {{ spec = "local.exportable_pkg@v1", source = "{self.lua_path(self.test_dir)}/exportable_pkg.lua" }}
}}
'''
        )

        _, export = self._install_and_export(
            "exportable_pkg.lua", "local.exportable_pkg@v1", manifest
        )
        self.assertEqual(export.returncode, 0, f"export failed: {export.stderr}")

        # Verify archive was created
        archive_path = Path(export.stdout.strip())
        self.assertTrue(archive_path.exists(), f"Archive not found: {archive_path}")
        self.assertTrue(
            archive_path.name.startswith("local.exportable_pkg@v1-"),
            f"Unexpected filename: {archive_path.name}",
        )
        self.assertTrue(
            archive_path.name.endswith(".tar.zst"),
            f"Expected .tar.zst extension: {archive_path.name}",
        )

    def test_export_default_package(self):
        """Export a default (non-EXPORTABLE) package produces fetch archive."""
        manifest = self.create_manifest(
            f'''
PACKAGES = {{
    {{ spec = "local.default_pkg@v1", source = "{self.lua_path(self.test_dir)}/default_pkg.lua" }}
}}
'''
        )

        _, export = self._install_and_export(
            "default_pkg.lua", "local.default_pkg@v1", manifest
        )
        self.assertEqual(export.returncode, 0, f"export failed: {export.stderr}")

        archive_path = Path(export.stdout.strip())
        self.assertTrue(archive_path.exists())
        self.assertTrue(archive_path.name.startswith("local.default_pkg@v1-"))

    def test_export_all_packages(self):
        """Export all packages when no query specified."""
        manifest = self.create_manifest(
            f'''
PACKAGES = {{
    {{ spec = "local.exportable_pkg@v1", source = "{self.lua_path(self.test_dir)}/exportable_pkg.lua" }},
    {{ spec = "local.default_pkg@v1", source = "{self.lua_path(self.test_dir)}/default_pkg.lua" }}
}}
'''
        )

        install = self.run_envy("install", "--manifest", str(manifest))
        self.assertEqual(install.returncode, 0, f"install failed: {install.stderr}")

        export = self.run_envy(
            "export",
            "-o", str(self.output_dir),
            "--manifest", str(manifest),
        )
        self.assertEqual(export.returncode, 0, f"export failed: {export.stderr}")

        # Should have two archive paths in stdout
        lines = [l for l in export.stdout.strip().split("\n") if l.strip()]
        self.assertEqual(len(lines), 2, f"Expected 2 archives, got: {lines}")

    def test_export_output_dir(self):
        """Verify -o flag places archives in specified directory."""
        manifest = self.create_manifest(
            f'''
PACKAGES = {{
    {{ spec = "local.exportable_pkg@v1", source = "{self.lua_path(self.test_dir)}/exportable_pkg.lua" }}
}}
'''
        )

        _, export = self._install_and_export(
            "exportable_pkg.lua", "local.exportable_pkg@v1", manifest
        )
        self.assertEqual(export.returncode, 0)

        archive_path = Path(export.stdout.strip())
        self.assertEqual(
            archive_path.parent,
            self.output_dir,
            f"Archive should be in output dir: {archive_path}",
        )

    def test_export_no_match_error(self):
        """Export with non-matching query produces error."""
        manifest = self.create_manifest(
            f'''
PACKAGES = {{
    {{ spec = "local.exportable_pkg@v1", source = "{self.lua_path(self.test_dir)}/exportable_pkg.lua" }}
}}
'''
        )

        install = self.run_envy("install", "--manifest", str(manifest))
        self.assertEqual(install.returncode, 0)

        export = self.run_envy(
            "export", "nonexistent",
            "--manifest", str(manifest),
        )
        self.assertEqual(export.returncode, 1)
        self.assertIn("no package matching", export.stderr.lower())

    def test_import_exported_package(self):
        """Export then import: verify package path is printed and usable."""
        manifest = self.create_manifest(
            f'''
PACKAGES = {{
    {{ spec = "local.exportable_pkg@v1", source = "{self.lua_path(self.test_dir)}/exportable_pkg.lua" }}
}}
'''
        )

        _, export = self._install_and_export(
            "exportable_pkg.lua", "local.exportable_pkg@v1", manifest
        )
        self.assertEqual(export.returncode, 0, f"export failed: {export.stderr}")
        archive_path = Path(export.stdout.strip())

        # Delete cache and reimport
        import_cache = Path(tempfile.mkdtemp(prefix="envy-import-test-"))
        try:
            result = test_config.run(
                [str(self.envy), "--cache-root", str(import_cache),
                 "import", str(archive_path)],
                cwd=self.project_root,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, f"import failed: {result.stderr}")

            pkg_path = Path(result.stdout.strip())
            self.assertTrue(
                pkg_path.exists(),
                f"Imported package path should exist: {pkg_path}",
            )
        finally:
            shutil.rmtree(import_cache, ignore_errors=True)

    def test_import_already_cached(self):
        """Importing a package that's already cached returns immediately."""
        manifest = self.create_manifest(
            f'''
PACKAGES = {{
    {{ spec = "local.exportable_pkg@v1", source = "{self.lua_path(self.test_dir)}/exportable_pkg.lua" }}
}}
'''
        )

        _, export = self._install_and_export(
            "exportable_pkg.lua", "local.exportable_pkg@v1", manifest
        )
        self.assertEqual(export.returncode, 0)
        archive_path = Path(export.stdout.strip())

        # Import into the SAME cache (already has the package)
        result = self.run_envy("import", str(archive_path))
        self.assertEqual(result.returncode, 0, f"import failed: {result.stderr}")

        pkg_path = Path(result.stdout.strip())
        self.assertTrue(pkg_path.exists())

    def test_import_bad_extension_error(self):
        """Importing a file without .tar.zst extension fails."""
        bad_file = self.output_dir / "bad.tar.gz"
        bad_file.write_text("not a real archive")

        result = self.run_envy("import", str(bad_file))
        self.assertEqual(result.returncode, 1)
        self.assertIn(".tar.zst", result.stderr)

    def test_export_partial_match(self):
        """Export supports partial identity matching."""
        manifest = self.create_manifest(
            f'''
PACKAGES = {{
    {{ spec = "local.exportable_pkg@v1", source = "{self.lua_path(self.test_dir)}/exportable_pkg.lua" }}
}}
'''
        )

        install = self.run_envy("install", "--manifest", str(manifest))
        self.assertEqual(install.returncode, 0, f"install failed: {install.stderr}")

        # Use partial match (name only)
        export = self.run_envy(
            "export", "exportable_pkg",
            "-o", str(self.output_dir),
            "--manifest", str(manifest),
        )
        self.assertEqual(export.returncode, 0, f"export failed: {export.stderr}")

        archive_path = Path(export.stdout.strip())
        self.assertTrue(archive_path.exists())

    def test_import_fetch_only_package(self):
        """Import a fetch-only archive (EXPORTABLE=false), verify fetch-only message."""
        manifest = self.create_manifest(
            f'''
PACKAGES = {{
    {{ spec = "local.default_pkg@v1", source = "{self.lua_path(self.test_dir)}/default_pkg.lua" }}
}}
'''
        )

        _, export = self._install_and_export(
            "default_pkg.lua", "local.default_pkg@v1", manifest
        )
        self.assertEqual(export.returncode, 0, f"export failed: {export.stderr}")
        archive_path = Path(export.stdout.strip())

        # Import into fresh cache
        import_cache = Path(tempfile.mkdtemp(prefix="envy-import-fetch-test-"))
        try:
            result = test_config.run(
                [str(self.envy), "--cache-root", str(import_cache),
                 "import", str(archive_path)],
                cwd=self.project_root,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, f"import failed: {result.stderr}")
            self.assertIn("fetch-only import", result.stdout)
        finally:
            shutil.rmtree(import_cache, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()

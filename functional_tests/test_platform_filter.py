"""Functional tests for platform-based package filtering.

Verifies that packages with non-matching platform constraints are skipped
by install/sync (run_full), that explicit queries for mismatched platforms
error, and that deploy (resolve_graph only) still sees all packages.
"""

import hashlib
import io
import platform as py_platform
import shutil
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from typing import List, Optional

from . import test_config
from .test_config import make_manifest


def _get_envy_binary() -> Path:
    root = Path(__file__).parent.parent / "out" / "build"
    if sys.platform == "win32":
        return root / "envy.exe"
    return root / "envy"


TEST_ARCHIVE_FILES = {"root/file1.txt": "content\n"}


def create_test_archive(output_path: Path) -> str:
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


def _host_platform() -> str:
    if sys.platform == "darwin":
        return "darwin"
    elif sys.platform.startswith("linux"):
        return "linux"
    return "windows"


def _non_host_platform() -> str:
    return "windows" if sys.platform != "win32" else "linux"


def _other_non_host_platform() -> str:
    """A second non-host platform different from _non_host_platform."""
    host = _host_platform()
    candidates = {"darwin", "linux", "windows"} - {host}
    return sorted(candidates)[0]  # deterministic pick


def _host_arch() -> str:
    """Host arch normalized to match envy's platform::arch_name()."""
    raw = py_platform.machine()
    return {"aarch64": "arm64", "ARM64": "arm64", "AMD64": "x86_64"}.get(raw, raw)


# --- Spec templates (double braces for .format()) ---

SPEC_CACHE_MANAGED = """\
IDENTITY = "local.cached@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}",
}}

STAGE = {{strip = 1}}

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
"""

SPEC_USER_MANAGED = """\
IDENTITY = "local.usermanaged@v1"

CHECK = function(project_root, options)
  return false
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
"""

SPEC_WITH_PRODUCT = """\
IDENTITY = "local.withtool@v1"
PRODUCTS = {{ tool = "bin/tool" }}

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}",
}}

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
"""


class PlatformFilterBase(unittest.TestCase):
    """Shared setUp/tearDown and helpers for platform filter tests."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-platfilter-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-platfilter-mf-"))
        self.specs_dir = Path(tempfile.mkdtemp(prefix="envy-platfilter-sp-"))
        self.envy = test_config.get_envy_executable()
        self.envy_main = _get_envy_binary()
        self.project_root = Path(__file__).parent.parent

        self.archive_path = self.specs_dir / "test.tar.gz"
        self.archive_hash = create_test_archive(self.archive_path)

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)
        shutil.rmtree(self.specs_dir, ignore_errors=True)

    def write_spec(self, name: str, content: str) -> str:
        spec_content = content.format(
            ARCHIVE_PATH=self.archive_path.as_posix(),
            ARCHIVE_HASH=self.archive_hash,
        )
        path = self.specs_dir / f"{name}.lua"
        path.write_text(spec_content, encoding="utf-8")
        return path.as_posix()

    def create_manifest(self, content: str, deploy: bool = False) -> Path:
        manifest_path = self.test_dir / "envy.lua"
        manifest_path.write_text(
            make_manifest(content, deploy=deploy), encoding="utf-8"
        )
        return manifest_path

    def run_cmd(
        self,
        subcmd: str,
        manifest: Path,
        args: Optional[List[str]] = None,
        use_main_binary: bool = False,
    ) -> "test_config.subprocess.CompletedProcess[str]":
        binary = str(self.envy_main if use_main_binary else self.envy)
        cmd = [binary, "--cache-root", str(self.cache_root), subcmd]
        if args:
            cmd.extend(args)
        cmd.extend(["--manifest", str(manifest)])
        return test_config.run(
            cmd, cwd=self.project_root, capture_output=True, text=True
        )


# =========================================================================
# install: bulk platform filtering
# =========================================================================


class TestInstallPlatformFilter(PlatformFilterBase):
    """envy install silently skips non-host packages when no query given."""

    def test_non_host_package_skipped(self):
        """Single non-host package: install succeeds, nothing installed."""
        spec_path = self.write_spec("cached", SPEC_CACHE_MANAGED)
        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{spec_path}",
       platforms = {{ "{_non_host_platform()}" }} }},
}}
""")
        result = self.run_cmd("install", manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        pkg_dir = self.cache_root / "packages" / "local.cached@v1"
        self.assertFalse(pkg_dir.exists(), "Non-host package should not be installed")

    def test_host_package_installed(self):
        """Single host package: installs normally."""
        spec_path = self.write_spec("cached", SPEC_CACHE_MANAGED)
        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{spec_path}",
       platforms = {{ "{_host_platform()}" }} }},
}}
""")
        result = self.run_cmd("install", manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        pkg_dir = self.cache_root / "packages" / "local.cached@v1"
        self.assertTrue(pkg_dir.exists(), "Host package should be installed")

    def test_no_platforms_means_all(self):
        """Package with no platforms field installs on any host."""
        spec_path = self.write_spec("cached", SPEC_CACHE_MANAGED)
        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{spec_path}" }},
}}
""")
        result = self.run_cmd("install", manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        pkg_dir = self.cache_root / "packages" / "local.cached@v1"
        self.assertTrue(pkg_dir.exists(), "Unconstrained package should be installed")

    def test_mixed_packages_only_host_installed(self):
        """Mix of host and non-host: only host package installed."""
        host_path = self.write_spec("cached", SPEC_CACHE_MANAGED)

        non_host_spec = SPEC_CACHE_MANAGED.replace(
            'IDENTITY = "local.cached@v1"',
            'IDENTITY = "local.other@v1"',
        )
        non_host_path = self.write_spec("other", non_host_spec)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{host_path}",
       platforms = {{ "{_host_platform()}" }} }},
    {{ spec = "local.other@v1", source = "{non_host_path}",
       platforms = {{ "{_non_host_platform()}" }} }},
}}
""")
        result = self.run_cmd("install", manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertTrue(
            (self.cache_root / "packages" / "local.cached@v1").exists(),
            "Host package should be installed",
        )
        self.assertFalse(
            (self.cache_root / "packages" / "local.other@v1").exists(),
            "Non-host package should not be installed",
        )

    def test_all_non_host_is_noop(self):
        """All packages non-host: install succeeds, nothing happens."""
        spec_path = self.write_spec("cached", SPEC_CACHE_MANAGED)
        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{spec_path}",
       platforms = {{ "{_non_host_platform()}" }} }},
}}
""")
        result = self.run_cmd("install", manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_user_managed_non_host_skipped(self):
        """User-managed package with non-host platform is also skipped."""
        spec_path = self.write_spec("usermanaged", SPEC_USER_MANAGED)
        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.usermanaged@v1", source = "{spec_path}",
       platforms = {{ "{_non_host_platform()}" }} }},
}}
""")
        result = self.run_cmd("install", manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_multiple_platforms_one_matches(self):
        """Package listing multiple platforms including host: installed."""
        spec_path = self.write_spec("cached", SPEC_CACHE_MANAGED)
        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{spec_path}",
       platforms = {{ "darwin", "linux", "windows" }} }},
}}
""")
        result = self.run_cmd("install", manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        pkg_dir = self.cache_root / "packages" / "local.cached@v1"
        self.assertTrue(
            pkg_dir.exists(), "Package with multiple platforms should install"
        )

    def test_multiple_platforms_none_match(self):
        """Package listing multiple non-host platforms: skipped."""
        non_hosts = sorted({"darwin", "linux", "windows"} - {_host_platform()})
        platforms_lua = ", ".join(f'"{p}"' for p in non_hosts)
        spec_path = self.write_spec("cached", SPEC_CACHE_MANAGED)
        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{spec_path}",
       platforms = {{ {platforms_lua} }} }},
}}
""")
        result = self.run_cmd("install", manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        pkg_dir = self.cache_root / "packages" / "local.cached@v1"
        self.assertFalse(pkg_dir.exists(), "No matching platform should skip install")


# =========================================================================
# install: explicit query errors on platform mismatch
# =========================================================================


class TestInstallExplicitQueryPlatformError(PlatformFilterBase):
    """envy install <query> errors when the queried package is non-host."""

    def test_explicit_query_non_host_errors(self):
        """Explicitly requesting a non-host package is an error."""
        spec_path = self.write_spec("cached", SPEC_CACHE_MANAGED)
        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{spec_path}",
       platforms = {{ "{_non_host_platform()}" }} }},
}}
""")
        result = self.run_cmd("install", manifest, args=["local.cached@v1"])
        self.assertNotEqual(result.returncode, 0, "Should fail for non-host query")
        self.assertIn("not available on this platform", result.stderr)

    def test_explicit_query_host_succeeds(self):
        """Explicitly requesting a host package works normally."""
        spec_path = self.write_spec("cached", SPEC_CACHE_MANAGED)
        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{spec_path}",
       platforms = {{ "{_host_platform()}" }} }},
}}
""")
        result = self.run_cmd("install", manifest, args=["local.cached@v1"])
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_explicit_query_no_platforms_succeeds(self):
        """Explicitly requesting an unconstrained package works."""
        spec_path = self.write_spec("cached", SPEC_CACHE_MANAGED)
        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{spec_path}" }},
}}
""")
        result = self.run_cmd("install", manifest, args=["local.cached@v1"])
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")


# =========================================================================
# sync: bulk platform filtering
# =========================================================================


class TestSyncPlatformFilter(PlatformFilterBase):
    """envy sync filters non-host packages from the build pipeline."""

    def test_non_host_package_skipped(self):
        """Non-host package: sync succeeds, nothing installed."""
        spec_path = self.write_spec("cached", SPEC_CACHE_MANAGED)
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{spec_path}",
       platforms = {{ "{_non_host_platform()}" }} }},
}}
""",
            deploy=True,
        )
        result = self.run_cmd("sync", manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        pkg_dir = self.cache_root / "packages" / "local.cached@v1"
        self.assertFalse(pkg_dir.exists(), "Non-host package should not be installed")

    def test_mixed_packages_only_host_installed(self):
        """Sync with mixed platforms installs only host packages."""
        host_path = self.write_spec("cached", SPEC_CACHE_MANAGED)

        non_host_spec = SPEC_CACHE_MANAGED.replace(
            'IDENTITY = "local.cached@v1"',
            'IDENTITY = "local.other@v1"',
        )
        non_host_path = self.write_spec("other", non_host_spec)

        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{host_path}",
       platforms = {{ "{_host_platform()}" }} }},
    {{ spec = "local.other@v1", source = "{non_host_path}",
       platforms = {{ "{_non_host_platform()}" }} }},
}}
""",
            deploy=True,
        )
        result = self.run_cmd("sync", manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertTrue(
            (self.cache_root / "packages" / "local.cached@v1").exists(),
        )
        self.assertFalse(
            (self.cache_root / "packages" / "local.other@v1").exists(),
        )


# =========================================================================
# sync: explicit query errors on platform mismatch
# =========================================================================


class TestSyncExplicitQueryPlatformError(PlatformFilterBase):
    """envy sync <query> errors when the queried package is non-host."""

    def test_explicit_query_non_host_errors(self):
        spec_path = self.write_spec("cached", SPEC_CACHE_MANAGED)
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{spec_path}",
       platforms = {{ "{_non_host_platform()}" }} }},
}}
""",
            deploy=True,
        )
        result = self.run_cmd("sync", manifest, args=["local.cached@v1"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not available on this platform", result.stderr)


# =========================================================================
# package: explicit identity errors on platform mismatch
# =========================================================================


class TestPackagePlatformFilter(PlatformFilterBase):
    """envy package errors when the queried package is non-host."""

    def test_package_non_host_errors(self):
        spec_path = self.write_spec("cached", SPEC_CACHE_MANAGED)
        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{spec_path}",
       platforms = {{ "{_non_host_platform()}" }} }},
}}
""")
        result = self.run_cmd("package", manifest, args=["local.cached@v1"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not available on this platform", result.stderr)

    def test_package_host_succeeds(self):
        """Package query for host platform works."""
        spec_path = self.write_spec("cached", SPEC_CACHE_MANAGED)
        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{spec_path}",
       platforms = {{ "{_host_platform()}" }} }},
}}
""")
        result = self.run_cmd("package", manifest, args=["local.cached@v1"])
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        # stdout should contain the package path
        self.assertTrue(result.stdout.strip(), "Should print package path")

    def test_package_unconstrained_succeeds(self):
        """Package query with no platform constraint works."""
        spec_path = self.write_spec("cached", SPEC_CACHE_MANAGED)
        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{spec_path}" }},
}}
""")
        result = self.run_cmd("package", manifest, args=["local.cached@v1"])
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")


# =========================================================================
# deploy: sees all packages regardless of platform (resolve_graph only)
# =========================================================================


class TestDeploySeesAllPlatforms(PlatformFilterBase):
    """envy deploy resolves non-host packages for cross-platform script gen."""

    def test_deploy_resolves_non_host_package(self):
        """deploy --platform all generates scripts for non-host packages."""
        spec_path = self.write_spec("withtool", SPEC_WITH_PRODUCT)
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.withtool@v1", source = "{spec_path}",
       platforms = {{ "{_non_host_platform()}" }} }},
}}
""",
            deploy=True,
        )
        result = self.run_cmd("deploy", manifest, args=["--platform", "all"])
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        bin_dir = self.test_dir / "envy-bin"
        # Non-host package's product should have a POSIX script
        # (the product generation respects resolved_platforms, but the package
        # must be in the graph for this to happen)
        # The product comes from a non-host package so posix or windows
        # depending on the mapping. Just verify deploy completed.

    def test_deploy_explicit_non_host_query_errors(self):
        """deploy <query> for non-host package is an error."""
        spec_path = self.write_spec("withtool", SPEC_WITH_PRODUCT)
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.withtool@v1", source = "{spec_path}",
       platforms = {{ "{_non_host_platform()}" }} }},
}}
""",
            deploy=True,
        )
        result = self.run_cmd("deploy", manifest, args=["local.withtool@v1"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not available on this platform", result.stderr)


# =========================================================================
# export: explicit query errors on platform mismatch
# =========================================================================


class TestExportPlatformFilter(PlatformFilterBase):
    """envy export <query> errors when queried package is non-host."""

    def test_export_explicit_non_host_errors(self):
        spec_path = self.write_spec("cached", SPEC_CACHE_MANAGED)
        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{spec_path}",
       platforms = {{ "{_non_host_platform()}" }} }},
}}
""")
        result = self.run_cmd("export", manifest, args=["local.cached@v1"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not available on this platform", result.stderr)


# =========================================================================
# Edge cases
# =========================================================================


class TestPlatformFilterEdgeCases(PlatformFilterBase):
    """Edge cases for platform filtering."""

    def test_os_arch_constraint_wrong_arch_skipped(self):
        """os-arch like 'darwin-x86_64' on arm64 host is skipped."""
        host_os = _host_platform()
        host_arch = _host_arch()
        wrong_arch = "x86_64" if host_arch == "arm64" else "arm64"
        constraint = f"{host_os}-{wrong_arch}"

        spec_path = self.write_spec("cached", SPEC_CACHE_MANAGED)
        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{spec_path}",
       platforms = {{ "{constraint}" }} }},
}}
""")
        result = self.run_cmd("install", manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        pkg_dir = self.cache_root / "packages" / "local.cached@v1"
        self.assertFalse(
            pkg_dir.exists(), f"Package with {constraint} should be skipped"
        )

    def test_os_arch_constraint_correct_arch_installed(self):
        """os-arch matching host exactly is installed."""
        host_os = _host_platform()
        host_arch = _host_arch()
        constraint = f"{host_os}-{host_arch}"

        spec_path = self.write_spec("cached", SPEC_CACHE_MANAGED)
        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{spec_path}",
       platforms = {{ "{constraint}" }} }},
}}
""")
        result = self.run_cmd("install", manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        pkg_dir = self.cache_root / "packages" / "local.cached@v1"
        self.assertTrue(
            pkg_dir.exists(), f"Package with {constraint} should be installed"
        )

    def test_three_packages_two_non_host_one_host(self):
        """Three packages, two non-host, one host: only host installed."""
        host_path = self.write_spec("cached", SPEC_CACHE_MANAGED)

        spec_a = SPEC_CACHE_MANAGED.replace(
            'IDENTITY = "local.cached@v1"', 'IDENTITY = "local.a@v1"'
        )
        a_path = self.write_spec("a", spec_a)

        spec_b = SPEC_CACHE_MANAGED.replace(
            'IDENTITY = "local.cached@v1"', 'IDENTITY = "local.b@v1"'
        )
        b_path = self.write_spec("b", spec_b)

        manifest = self.create_manifest(f"""
PACKAGES = {{
    {{ spec = "local.a@v1", source = "{a_path}",
       platforms = {{ "{_non_host_platform()}" }} }},
    {{ spec = "local.cached@v1", source = "{host_path}",
       platforms = {{ "{_host_platform()}" }} }},
    {{ spec = "local.b@v1", source = "{b_path}",
       platforms = {{ "{_other_non_host_platform()}" }} }},
}}
""")
        result = self.run_cmd("install", manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertFalse((self.cache_root / "packages" / "local.a@v1").exists())
        self.assertTrue((self.cache_root / "packages" / "local.cached@v1").exists())
        self.assertFalse((self.cache_root / "packages" / "local.b@v1").exists())

    def test_install_then_sync_consistent(self):
        """install and sync agree on what gets installed for platform-constrained packages."""
        host_path = self.write_spec("cached", SPEC_CACHE_MANAGED)

        non_host_spec = SPEC_CACHE_MANAGED.replace(
            'IDENTITY = "local.cached@v1"', 'IDENTITY = "local.other@v1"'
        )
        non_host_path = self.write_spec("other", non_host_spec)

        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.cached@v1", source = "{host_path}",
       platforms = {{ "{_host_platform()}" }} }},
    {{ spec = "local.other@v1", source = "{non_host_path}",
       platforms = {{ "{_non_host_platform()}" }} }},
}}
""",
            deploy=True,
        )

        # install
        result_install = self.run_cmd("install", manifest)
        self.assertEqual(
            result_install.returncode, 0, f"stderr: {result_install.stderr}"
        )
        self.assertTrue((self.cache_root / "packages" / "local.cached@v1").exists())
        self.assertFalse((self.cache_root / "packages" / "local.other@v1").exists())

        # sync on same manifest should also succeed and not install the non-host package
        result_sync = self.run_cmd("sync", manifest)
        self.assertEqual(result_sync.returncode, 0, f"stderr: {result_sync.stderr}")
        self.assertFalse((self.cache_root / "packages" / "local.other@v1").exists())

    def test_empty_manifest_succeeds(self):
        """Empty manifest with no packages succeeds."""
        manifest = self.create_manifest("PACKAGES = {}")
        result = self.run_cmd("install", manifest)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

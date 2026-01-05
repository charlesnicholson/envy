#!/usr/bin/env python3
"""Functional tests for bundle fetching and spec resolution.

Tests bundle fetching from local directories, identity verification,
SPECS->IDENTITY validation, and spec-from-bundle resolution.
"""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from . import test_config
from .test_config import make_manifest


class TestBundleFetchLocal(unittest.TestCase):
    """Tests for fetching bundles from local directories."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-bundle-test-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-bundle-manifest-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent
        self.test_data = self.project_root / "test_data"

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)

    @staticmethod
    def lua_path(path: Path) -> str:
        """Convert path to Lua-safe string."""
        return path.as_posix()

    def create_manifest(self, content: str) -> Path:
        """Create manifest file with given content."""
        manifest_path = self.test_dir / "envy.lua"
        manifest_path.write_text(make_manifest(content), encoding="utf-8")
        return manifest_path

    def run_sync(self, manifest: Path, install_all: bool = True):
        """Run 'envy sync' command and return result."""
        cmd = [
            str(self.envy),
            "--cache-root",
            str(self.cache_root),
            "sync",
            "--manifest",
            str(manifest),
        ]
        if install_all:
            cmd.append("--install-all")
        return subprocess.run(
            cmd,
            cwd=self.project_root,
            capture_output=True,
            text=True,
        )

    def test_fetch_spec_from_local_bundle(self):
        """Fetch a spec from a local bundle directory."""
        bundle_path = self.test_data / "bundles" / "simple-bundle"
        manifest = self.create_manifest(
            f"""
BUNDLES = {{
    toolchain = {{
        identity = "test.simple-bundle@v1",
        source = "{self.lua_path(bundle_path)}",
    }},
}}

PACKAGES = {{
    {{ spec = "test.spec_a@v1", bundle = "toolchain" }},
}}
"""
        )

        result = self.run_sync(manifest=manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertIn("installed", result.stderr.lower())

        # Verify package was installed
        pkg_path = self.cache_root / "packages" / "test.spec_a@v1"
        self.assertTrue(pkg_path.exists(), f"Expected {pkg_path} to exist")

    def test_fetch_multiple_specs_from_same_bundle(self):
        """Multiple specs from same bundle share one fetch."""
        bundle_path = self.test_data / "bundles" / "simple-bundle"
        manifest = self.create_manifest(
            f"""
BUNDLES = {{
    toolchain = {{
        identity = "test.simple-bundle@v1",
        source = "{self.lua_path(bundle_path)}",
    }},
}}

PACKAGES = {{
    {{ spec = "test.spec_a@v1", bundle = "toolchain" }},
    {{ spec = "test.spec_b@v1", bundle = "toolchain" }},
}}
"""
        )

        result = self.run_sync(manifest=manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Verify both packages installed
        pkg_a = self.cache_root / "packages" / "test.spec_a@v1"
        pkg_b = self.cache_root / "packages" / "test.spec_b@v1"
        self.assertTrue(pkg_a.exists(), f"Expected {pkg_a} to exist")
        self.assertTrue(pkg_b.exists(), f"Expected {pkg_b} to exist")

        # Verify bundle was cached once
        spec_cache = self.cache_root / "specs" / "test.simple-bundle@v1"
        self.assertTrue(spec_cache.exists(), f"Expected bundle cache at {spec_cache}")

    def test_inline_bundle_declaration(self):
        """Inline bundle declaration in package entry."""
        bundle_path = self.test_data / "bundles" / "simple-bundle"
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{
        spec = "test.spec_a@v1",
        bundle = {{
            identity = "test.simple-bundle@v1",
            source = "{self.lua_path(bundle_path)}",
        }},
    }},
}}
"""
        )

        result = self.run_sync(manifest=manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        pkg_path = self.cache_root / "packages" / "test.spec_a@v1"
        self.assertTrue(pkg_path.exists(), f"Expected {pkg_path} to exist")


class TestBundleIdentityVerification(unittest.TestCase):
    """Tests for bundle identity verification."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-bundle-identity-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-bundle-manifest-"))
        self.bundle_dir = Path(tempfile.mkdtemp(prefix="envy-test-bundle-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)
        shutil.rmtree(self.bundle_dir, ignore_errors=True)

    @staticmethod
    def lua_path(path: Path) -> str:
        return path.as_posix()

    def create_manifest(self, content: str) -> Path:
        manifest_path = self.test_dir / "envy.lua"
        manifest_path.write_text(make_manifest(content), encoding="utf-8")
        return manifest_path

    def create_bundle(self, bundle_identity: str, specs: dict[str, str]) -> Path:
        """Create a bundle directory with given identity and specs."""
        specs_dir = self.bundle_dir / "specs"
        specs_dir.mkdir(parents=True, exist_ok=True)

        # Create envy-bundle.lua
        specs_lua = ",\n".join(
            f'  ["{spec_id}"] = "{path}"' for spec_id, path in specs.items()
        )
        bundle_lua = f"""BUNDLE = "{bundle_identity}"
SPECS = {{
{specs_lua}
}}
"""
        (self.bundle_dir / "envy-bundle.lua").write_text(bundle_lua)

        # Create spec files
        for spec_id, path in specs.items():
            spec_path = self.bundle_dir / path
            spec_path.parent.mkdir(parents=True, exist_ok=True)
            spec_lua = f"""IDENTITY = "{spec_id}"
DEPENDENCIES = {{}}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
"""
            spec_path.write_text(spec_lua)

        return self.bundle_dir

    def run_sync(self, manifest: Path):
        cmd = [
            str(self.envy),
            "--cache-root",
            str(self.cache_root),
            "sync",
            "--install-all",
            "--manifest",
            str(manifest),
        ]
        return subprocess.run(
            cmd,
            cwd=self.project_root,
            capture_output=True,
            text=True,
        )

    def test_bundle_identity_mismatch_errors(self):
        """Bundle identity mismatch produces error."""
        # Create bundle with identity "actual.bundle@v1"
        bundle_path = self.create_bundle(
            "actual.bundle@v1", {"test.spec@v1": "specs/spec.lua"}
        )

        # But manifest declares "expected.bundle@v1"
        manifest = self.create_manifest(
            f"""
BUNDLES = {{
    mybundle = {{
        identity = "expected.bundle@v1",
        source = "{self.lua_path(bundle_path)}",
    }},
}}

PACKAGES = {{
    {{ spec = "test.spec@v1", bundle = "mybundle" }},
}}
"""
        )

        result = self.run_sync(manifest=manifest)

        self.assertNotEqual(result.returncode, 0, "Expected non-zero exit code")
        self.assertIn("identity mismatch", result.stderr.lower())
        self.assertIn("expected.bundle@v1", result.stderr)
        self.assertIn("actual.bundle@v1", result.stderr)

    def test_spec_identity_mismatch_errors(self):
        """Spec IDENTITY mismatch in bundle produces error."""
        # Create bundle where SPECS declares "test.expected@v1" but file has "test.actual@v1"
        specs_dir = self.bundle_dir / "specs"
        specs_dir.mkdir(parents=True, exist_ok=True)

        bundle_lua = """BUNDLE = "test.bundle@v1"
SPECS = {
  ["test.expected@v1"] = "specs/spec.lua"
}
"""
        (self.bundle_dir / "envy-bundle.lua").write_text(bundle_lua)

        # Spec file declares different identity
        spec_lua = """IDENTITY = "test.actual@v1"
DEPENDENCIES = {}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
"""
        (specs_dir / "spec.lua").write_text(spec_lua)

        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{
        spec = "test.expected@v1",
        bundle = {{
            identity = "test.bundle@v1",
            source = "{self.lua_path(self.bundle_dir)}",
        }},
    }},
}}
"""
        )

        result = self.run_sync(manifest=manifest)

        self.assertNotEqual(result.returncode, 0, "Expected non-zero exit code")
        self.assertIn("identity mismatch", result.stderr.lower())

    def test_spec_not_in_bundle_errors(self):
        """Requesting spec not in bundle SPECS produces error."""
        bundle_path = self.create_bundle(
            "test.bundle@v1", {"test.exists@v1": "specs/exists.lua"}
        )

        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{
        spec = "test.missing@v1",
        bundle = {{
            identity = "test.bundle@v1",
            source = "{self.lua_path(bundle_path)}",
        }},
    }},
}}
"""
        )

        result = self.run_sync(manifest=manifest)

        self.assertNotEqual(result.returncode, 0, "Expected non-zero exit code")
        self.assertIn("not found in bundle", result.stderr.lower())
        self.assertIn("test.missing@v1", result.stderr)


class TestBundleAliasResolution(unittest.TestCase):
    """Tests for BUNDLES alias resolution."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-bundle-alias-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-bundle-manifest-"))
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

    def run_sync(self, manifest: Path):
        cmd = [
            str(self.envy),
            "--cache-root",
            str(self.cache_root),
            "sync",
            "--install-all",
            "--manifest",
            str(manifest),
        ]
        return subprocess.run(
            cmd,
            cwd=self.project_root,
            capture_output=True,
            text=True,
        )

    def test_unknown_bundle_alias_errors(self):
        """Unknown bundle alias produces error."""
        manifest = self.create_manifest(
            """
PACKAGES = {
    { spec = "test.spec@v1", bundle = "nonexistent" },
}
"""
        )

        result = self.run_sync(manifest=manifest)

        self.assertNotEqual(result.returncode, 0, "Expected non-zero exit code")
        self.assertIn("nonexistent", result.stderr.lower())
        self.assertIn("not found", result.stderr.lower())

    def test_cannot_have_both_source_and_bundle(self):
        """Package cannot specify both source and bundle."""
        bundle_path = self.test_data / "bundles" / "simple-bundle"
        manifest = self.create_manifest(
            f"""
BUNDLES = {{
    toolchain = {{
        identity = "test.simple-bundle@v1",
        source = "{self.lua_path(bundle_path)}",
    }},
}}

PACKAGES = {{
    {{
        spec = "test.spec_a@v1",
        source = "./some/path.lua",
        bundle = "toolchain",
    }},
}}
"""
        )

        result = self.run_sync(manifest=manifest)

        self.assertNotEqual(result.returncode, 0, "Expected non-zero exit code")
        self.assertIn("source", result.stderr.lower())
        self.assertIn("bundle", result.stderr.lower())


if __name__ == "__main__":
    unittest.main()

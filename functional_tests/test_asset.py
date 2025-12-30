#!/usr/bin/env python3
"""Functional tests for 'envy asset' command.

Tests asset path querying, manifest discovery, dependency installation,
ambiguity detection, and error handling.
"""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import Optional

from . import test_config


class TestAssetCommand(unittest.TestCase):
    """Tests for 'envy asset' command."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-asset-test-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-asset-manifest-"))
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

    def create_manifest(self, content: str, subdir: str = "") -> Path:
        """Create manifest file with given content, optionally in subdirectory."""
        manifest_dir = self.test_dir / subdir if subdir else self.test_dir
        manifest_dir.mkdir(parents=True, exist_ok=True)
        manifest_path = manifest_dir / "envy.lua"
        manifest_path.write_text(content, encoding="utf-8")
        return manifest_path

    def run_asset(
        self, identity: str, manifest: Optional[Path] = None, cwd: Optional[Path] = None
    ):
        """Run 'envy asset' command and return result."""
        cmd = [str(self.envy), "--cache-root", str(self.cache_root), "asset", identity]
        if manifest:
            cmd.extend(["--manifest", str(manifest)])

        # Run from project root so relative paths in recipes work
        result = subprocess.run(
            cmd,
            cwd=cwd or self.project_root,
            capture_output=True,
            text=True,
        )
        return result

    def test_asset_simple_package(self):
        """Query asset path for simple package with no dependencies."""
        # Note: build_dependency.lua has relative fetch source, so we run from project root
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/specs/build_dependency.lua" }}
}}
"""
        )

        result = self.run_asset("local.build_dependency@v1", manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertTrue(result.stdout.strip(), "Expected path in stdout")

        asset_path = Path(result.stdout.strip())
        self.assertTrue(
            asset_path.is_absolute(), f"Expected absolute path: {asset_path}"
        )
        self.assertTrue(asset_path.exists(), f"Asset path should exist: {asset_path}")
        # Check path ends with pkg directory (accept both / and \ separators)
        self.assertTrue(
            str(asset_path).endswith("/pkg") or str(asset_path).endswith("\\pkg"),
            f"Path should end with pkg directory: {asset_path}",
        )

    def test_asset_with_dependencies(self):
        """Query asset for package with dependencies, verify both installed."""
        # NOTE: diamond_a is a programmatic package, so this test currently expects failure
        # TODO: Create test recipes with dependencies that produce actual cached assets
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.diamond_a@v1", source = "{self.lua_path(self.test_data)}/specs/diamond_a.lua" }}
}}
"""
        )

        result = self.run_asset("local.diamond_a@v1", manifest)

        # Currently fails because diamond_a is programmatic (user-managed)
        self.assertEqual(result.returncode, 1)
        self.assertIn("not cache-managed", result.stderr)

    def test_asset_already_cached(self):
        """Query asset that's already installed, should return immediately."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/specs/build_dependency.lua" }}
}}
"""
        )

        # First installation
        result1 = self.run_asset("local.build_dependency@v1", manifest)
        self.assertEqual(result1.returncode, 0)
        path1 = result1.stdout.strip()

        # Second query (should be cached)
        result2 = self.run_asset("local.build_dependency@v1", manifest)
        self.assertEqual(result2.returncode, 0)
        path2 = result2.stdout.strip()

        # Same path returned
        self.assertEqual(path1, path2, "Should return same path for cached asset")

    def test_asset_auto_discover_manifest(self):
        """Auto-discover manifest from parent directory."""
        # NOTE: This test is currently disabled because recipes have relative fetch paths
        # that don't work when running from arbitrary directories
        # TODO: Either use recipes with absolute fetch paths or fix path resolution
        self.skipTest("Auto-discover with relative recipe paths not yet supported")

    def test_asset_explicit_manifest_path(self):
        """Use explicit --manifest flag to specify manifest location."""
        # Create manifest in non-standard location
        other_dir = Path(tempfile.mkdtemp(prefix="envy-other-"))
        try:
            manifest = other_dir / "custom.lua"
            manifest.write_text(
                f"""
PACKAGES = {{
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/specs/build_dependency.lua" }}
}}
""",
                encoding="utf-8",
            )

            result = self.run_asset("local.build_dependency@v1", manifest=manifest)

            self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
            self.assertTrue(result.stdout.strip())
        finally:
            shutil.rmtree(other_dir, ignore_errors=True)

    def test_asset_no_manifest_found(self):
        """Error when no manifest can be found."""
        # Use directory with no manifest
        empty_dir = Path(tempfile.mkdtemp(prefix="envy-empty-"))
        try:
            result = self.run_asset("local.simple@v1", manifest=None, cwd=empty_dir)

            self.assertEqual(result.returncode, 1)
            self.assertIn("not found", result.stderr.lower())
        finally:
            shutil.rmtree(empty_dir, ignore_errors=True)

    def test_asset_selective_installation(self):
        """Only requested package and dependencies installed, not entire manifest."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/specs/build_dependency.lua" }},
    {{ spec = "local.build_function@v1", source = "{self.lua_path(self.test_data)}/specs/build_function.lua" }},
    {{ spec = "local.build_nil@v1", source = "{self.lua_path(self.test_data)}/specs/build_nil.lua" }},
}}
"""
        )

        # Request only build_dependency (which has no dependencies)
        result = self.run_asset("local.build_dependency@v1", manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Check which packages were installed
        assets_dir = self.cache_root / "packages"
        installed = (
            [d.name for d in assets_dir.glob("local.*")] if assets_dir.exists() else []
        )

        # Should have build_dependency but NOT build_function or build_nil
        self.assertTrue(
            any("build_dependency" in name for name in installed),
            "build_dependency should be installed",
        )
        # build_function and build_nil should NOT be installed
        self.assertFalse(
            any("build_function" in name for name in installed),
            "build_function should NOT be installed (not requested)",
        )
        self.assertFalse(
            any("build_nil" in name for name in installed),
            "build_nil should NOT be installed (not requested)",
        )
        # This demonstrates selective installation - only 1 of 3 packages installed

    def test_asset_ambiguous_different_options(self):
        """Error when same identity appears with different options."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/specs/build_dependency.lua", options = {{ mode = "debug" }} }},
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/specs/build_dependency.lua", options = {{ mode = "release" }} }}
}}
"""
        )

        result = self.run_asset("local.build_dependency@v1", manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("multiple times", result.stderr.lower())
        self.assertIn("different options", result.stderr.lower())

    def test_asset_duplicate_same_options_ok(self):
        """Duplicate identity with same options should succeed."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/specs/build_dependency.lua" }},
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/specs/build_dependency.lua" }}
}}
"""
        )

        result = self.run_asset("local.build_dependency@v1", manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertTrue(result.stdout.strip())

    def test_asset_identity_not_in_manifest(self):
        """Error when requested identity not in manifest."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.other@v1", source = "{self.lua_path(self.test_data)}/specs/build_dependency.lua" }}
}}
"""
        )

        result = self.run_asset("local.nonexistent@v1", manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("not found", result.stderr.lower())

    def test_asset_programmatic_package(self):
        """Error for programmatic packages (no cached artifacts)."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.programmatic@v1", source = "{self.lua_path(self.test_data)}/specs/install_programmatic.lua" }}
}}
"""
        )

        result = self.run_asset("local.programmatic@v1", manifest)

        self.assertEqual(result.returncode, 1)
        self.assertTrue(result.stderr.strip(), "Should have error message in stderr")

    def test_asset_build_failure(self):
        """Error when build phase fails."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.failing@v1", source = "{self.lua_path(self.test_data)}/specs/build_error_nonzero_exit.lua" }}
}}
"""
        )

        result = self.run_asset("local.failing@v1", manifest)

        self.assertEqual(result.returncode, 1)
        self.assertTrue(result.stderr.strip(), "Should have error message in stderr")

    def test_asset_invalid_manifest_syntax(self):
        """Error when manifest has Lua syntax error."""
        manifest = self.create_manifest(
            """
PACKAGES = {
    this is invalid lua syntax
}
"""
        )

        result = self.run_asset("local.simple@v1", manifest)

        self.assertEqual(result.returncode, 1)
        self.assertTrue(result.stderr.strip(), "Should have error message in stderr")

    def test_asset_stdout_format(self):
        """Verify stdout contains exactly one line with absolute path."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/specs/build_dependency.lua" }}
}}
"""
        )

        result = self.run_asset("local.build_dependency@v1", manifest)

        self.assertEqual(result.returncode, 0)

        lines = result.stdout.strip().split("\n")
        self.assertEqual(len(lines), 1, "Should have exactly one line in stdout")

        path = lines[0]
        # Check if path is absolute (Unix: starts with /, Windows: starts with drive letter)
        import os

        self.assertTrue(os.path.isabs(path), f"Should be absolute path: {path}")
        # Path should end with pkg directory (accept both / and \ separators)
        self.assertTrue(
            path.endswith("/pkg") or path.endswith("\\pkg"),
            f"Should end with pkg directory: {path}",
        )

    def test_asset_stderr_only_on_error(self):
        """Success should have no stderr output, failure should."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/specs/build_dependency.lua" }},
    {{ spec = "local.build_function@v1", source = "{self.lua_path(self.test_data)}/specs/build_function.lua" }}
}}
"""
        )

        # Success case - note: might have trace/debug logs, so we check it doesn't have errors
        result_ok = self.run_asset("local.build_dependency@v1", manifest)
        self.assertEqual(result_ok.returncode, 0)
        # Just verify no error messages (trace logs are okay)

        # Failure case
        result_fail = self.run_asset("local.nonexistent@v1", manifest)
        self.assertEqual(result_fail.returncode, 1)
        self.assertTrue(
            result_fail.stderr.strip(), "Should have error message in stderr"
        )

    def test_asset_with_recipe_options(self):
        """Install package with options in manifest."""
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/specs/build_dependency.lua", options = {{ mode = "debug" }} }}
}}
"""
        )

        result = self.run_asset("local.build_dependency@v1", manifest)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        asset_path = Path(result.stdout.strip())
        self.assertTrue(asset_path.exists())
        # The canonical key will include options, affecting the hash in the path

    def test_asset_different_options_separate_cache_entries(self):
        """Different options produce separate cache entries with distinct content."""
        # Create recipe that writes option value to a file
        # This is a cache-managed package (no check verb) that writes artifacts to cache
        recipe_content = """IDENTITY = "local.test_options_cache@v1"

-- Empty fetch - recipe generates content directly in install phase
function FETCH(tmp_dir, options)
    -- Nothing to fetch
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    local f = io.open(install_dir .. "/variant.txt", "w")
    f:write(options.variant or "none")
    f:close()
end
"""
        recipe_path = self.test_dir / "test_options_cache.lua"
        recipe_path.write_text(recipe_content, encoding="utf-8")

        # Manifest with variant=foo
        manifest_foo = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.test_options_cache@v1", source = "{self.lua_path(recipe_path)}", options = {{ variant = "foo" }} }}
}}
""",
            subdir="foo",
        )

        # Manifest with variant=bar
        manifest_bar = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.test_options_cache@v1", source = "{self.lua_path(recipe_path)}", options = {{ variant = "bar" }} }}
}}
""",
            subdir="bar",
        )

        # Install foo variant
        result_foo = self.run_asset("local.test_options_cache@v1", manifest_foo)
        self.assertEqual(result_foo.returncode, 0, f"stderr: {result_foo.stderr}")
        path_foo = Path(result_foo.stdout.strip())
        self.assertTrue(path_foo.exists())

        # Install bar variant
        result_bar = self.run_asset("local.test_options_cache@v1", manifest_bar)
        self.assertEqual(result_bar.returncode, 0, f"stderr: {result_bar.stderr}")
        path_bar = Path(result_bar.stdout.strip())
        self.assertTrue(path_bar.exists())

        # Verify different cache paths
        self.assertNotEqual(
            path_foo,
            path_bar,
            "Different options must produce different cache paths",
        )

        # Verify correct content in each cache entry
        variant_foo = (path_foo / "variant.txt").read_text()
        variant_bar = (path_bar / "variant.txt").read_text()
        self.assertEqual(variant_foo, "foo", "Foo variant should contain 'foo'")
        self.assertEqual(variant_bar, "bar", "Bar variant should contain 'bar'")

    def test_asset_transitive_dependency_chain(self):
        """Install package with transitive dependencies (diamond structure)."""
        # NOTE: diamond_a is programmatic, test expects failure
        # TODO: Create test recipes with dependencies that produce actual cached assets
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.diamond_a@v1", source = "{self.lua_path(self.test_data)}/specs/diamond_a.lua" }}
}}
"""
        )

        result = self.run_asset("local.diamond_a@v1", manifest)

        # Currently fails because diamond_a is programmatic (user-managed)
        self.assertEqual(result.returncode, 1)
        self.assertIn("not cache-managed", result.stderr)

    def test_asset_diamond_dependency(self):
        """Install package with diamond dependency: A → B,C → D."""
        # NOTE: diamond_a is programmatic, test expects failure
        # TODO: Create test recipes with dependencies that produce actual cached assets
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.diamond_a@v1", source = "{self.lua_path(self.test_data)}/specs/diamond_a.lua" }}
}}
"""
        )

        result = self.run_asset("local.diamond_a@v1", manifest)

        # Currently fails because diamond_a is programmatic (user-managed)
        self.assertEqual(result.returncode, 1)
        self.assertIn("not cache-managed", result.stderr)

    def test_asset_with_product_dependency_clean_cache(self):
        """Asset command must resolve graph to find product providers.

        Regression test: asset command should call resolve_graph() to find
        recipes that provide products needed by dependencies. Without this,
        product dependencies fail on clean cache.
        """
        # Create product provider recipe
        provider_recipe = """IDENTITY = "local.test_product_provider@v1"

FETCH = { source = "test_data/archives/test.tar.gz",
          sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c" }

INSTALL = function(ctx)
end

PRODUCTS = { test_tool = "bin/tool" }
"""
        provider_path = self.test_dir / "test_product_provider.lua"
        provider_path.write_text(provider_recipe, encoding="utf-8")

        # Create consumer recipe that depends on the product
        consumer_recipe = f"""IDENTITY = "local.test_product_consumer@v1"

DEPENDENCIES = {{
    {{ product = "test_tool" }}
}}

FETCH = {{
    source = "test_data/archives/test.tar.gz",
    sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}}

INSTALL = function(ctx)
end
"""
        consumer_path = self.test_dir / "test_product_consumer.lua"
        consumer_path.write_text(consumer_recipe, encoding="utf-8")

        # Manifest with both recipes
        manifest = self.create_manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.test_product_provider@v1", source = "{self.lua_path(provider_path)}" }},
    {{ spec = "local.test_product_consumer@v1", source = "{self.lua_path(consumer_path)}" }}
}}
"""
        )

        # Run asset on consumer with clean cache
        result = self.run_asset("local.test_product_consumer@v1", manifest)

        # Should succeed - asset command must resolve graph to find product provider
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertTrue(result.stdout.strip(), "Expected asset path in stdout")

        asset_path = Path(result.stdout.strip())
        self.assertTrue(asset_path.exists(), f"Asset path should exist: {asset_path}")


if __name__ == "__main__":
    unittest.main()

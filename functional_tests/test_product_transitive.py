#!/usr/bin/env python3
"""Functional tests for transitive product provision."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from . import test_config


class TestProductTransitive(unittest.TestCase):
    """Test transitive product provision validation."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-transitive-cache-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-transitive-manifest-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent
        self.test_data = self.project_root / "test_data"

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def lua_path(self, path: Path) -> str:
        return path.as_posix()

    def manifest(self, content: str) -> Path:
        manifest_path = self.test_dir / "envy.lua"
        manifest_path.write_text(content, encoding="utf-8")
        return manifest_path

    def run_envy(self, args):
        cmd = [str(self.envy), "--cache-root", str(self.cache_root), *args]
        return subprocess.run(
            cmd, cwd=self.project_root, capture_output=True, text=True
        )

    def test_transitive_provision_chain_success(self):
        """Weak dep fallback transitively provides via dependency chain (A→B→C, C provides)."""
        # Root has weak dep on "tool" with fallback to intermediate
        # Intermediate depends on provider
        # Provider actually provides "tool"
        # Validation should succeed because intermediate transitively provides "tool"
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.product_transitive_root@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_transitive_root.lua",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Verify all three specs were installed
        root_dir = self.cache_root / "packages" / "local.product_transitive_root@v1"
        intermediate_dir = (
            self.cache_root / "packages" / "local.product_transitive_intermediate@v1"
        )
        provider_dir = (
            self.cache_root / "packages" / "local.product_transitive_provider@v1"
        )

        self.assertTrue(root_dir.exists(), f"missing {root_dir}")
        self.assertTrue(intermediate_dir.exists(), f"missing {intermediate_dir}")
        self.assertTrue(provider_dir.exists(), f"missing {provider_dir}")

    def test_fallback_doesnt_transitively_provide_error(self):
        """Weak dep fallback that doesn't transitively provide should fail validation."""
        # Root has weak dep on "tool" with fallback to intermediate_no_provide
        # Intermediate has no products and no dependencies
        # Validation should fail because intermediate can't provide "tool"
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.product_transitive_root_fail@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_transitive_root_fail.lua",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertNotEqual(
            result.returncode, 0, "Expected failure for non-transitive fallback"
        )

        # Should mention validation failure and the product name
        stderr_lower = result.stderr.lower()
        self.assertTrue(
            "tool" in stderr_lower
            and ("does not provide" in stderr_lower or "fallback" in stderr_lower),
            f"Expected validation error mentioning product 'tool', got: {result.stderr}",
        )

    def test_fallback_transitively_provides_via_dependency(self):
        """Fallback with dependency that provides product should pass validation."""
        # Same as test_transitive_provision_chain_success but emphasizes
        # that fallback itself doesn't provide, only its dependency does
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.product_transitive_root@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_transitive_root.lua",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Verify provider was installed (intermediate doesn't provide, but its dep does)
        provider_dir = (
            self.cache_root / "packages" / "local.product_transitive_provider@v1"
        )
        self.assertTrue(
            provider_dir.exists(),
            "Transitive provider should be installed via fallback dependency",
        )

    def test_transitive_provision_with_existing_provider(self):
        """When provider exists in manifest, weak dep should use it instead of fallback."""
        # Include direct provider in manifest alongside root with weak dep
        # Resolution should prefer the direct provider over fallback's transitive provision
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.product_transitive_provider@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_transitive_provider.lua",
  }},
  {{
    spec = "local.product_transitive_root@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_transitive_root.lua",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        # Both provider and root should exist
        # Intermediate might not be installed since provider was found directly
        root_dir = self.cache_root / "packages" / "local.product_transitive_root@v1"
        provider_dir = (
            self.cache_root / "packages" / "local.product_transitive_provider@v1"
        )

        self.assertTrue(root_dir.exists(), f"missing {root_dir}")
        self.assertTrue(provider_dir.exists(), f"missing {provider_dir}")

    def test_deep_transitive_chain(self):
        """Verify transitive provision works through multiple levels."""
        # Create a 4-level chain: consumer → mid1 → mid2 → provider
        mid2_lua = f"""
IDENTITY = "local.transitive_mid2@v1"

DEPENDENCIES = {{
  {{
    spec = "local.product_transitive_provider@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_transitive_provider.lua",
  }}
}}

FETCH = {{
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}}

INSTALL = function(ctx)
end
"""
        mid2_path = self.test_dir / "transitive_mid2.lua"
        mid2_path.write_text(mid2_lua, encoding="utf-8")

        mid1_lua = f"""
IDENTITY = "local.transitive_mid1@v1"

DEPENDENCIES = {{
  {{
    spec = "local.transitive_mid2@v1",
    source = "{self.lua_path(mid2_path)}",
  }}
}}

FETCH = {{
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}}

INSTALL = function(ctx)
end
"""
        mid1_path = self.test_dir / "transitive_mid1.lua"
        mid1_path.write_text(mid1_lua, encoding="utf-8")

        consumer_lua = f"""
IDENTITY = "local.transitive_consumer@v1"

DEPENDENCIES = {{
  {{
    product = "tool",
    weak = {{
      spec = "local.transitive_mid1@v1",
      source = "{self.lua_path(mid1_path)}",
    }}
  }}
}}

FETCH = {{
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}}

INSTALL = function(ctx)
end
"""
        consumer_path = self.test_dir / "transitive_consumer.lua"
        consumer_path.write_text(consumer_lua, encoding="utf-8")

        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.transitive_consumer@v1",
    source = "{self.lua_path(consumer_path)}",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertEqual(
            result.returncode,
            0,
            f"Deep transitive chain should succeed. stderr: {result.stderr}",
        )

        # Verify provider at the end was reached
        provider_dir = (
            self.cache_root / "packages" / "local.product_transitive_provider@v1"
        )
        self.assertTrue(
            provider_dir.exists(), "Provider should be installed via deep chain"
        )


if __name__ == "__main__":
    unittest.main()

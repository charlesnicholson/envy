#!/usr/bin/env python3
"""Functional coverage for ctx.asset() and ctx.product() enforcement."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from . import test_config


class TestCtxAssetProduct(unittest.TestCase):
    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-ctx-asset-product-cache-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-ctx-asset-product-manifest-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent
        self.test_data = self.project_root / "test_data"

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def manifest(self, content: str) -> Path:
        manifest_path = self.test_dir / "envy.lua"
        manifest_path.write_text(content, encoding="utf-8")
        return manifest_path

    def run_envy(self, args):
        cmd = [str(self.envy), *args, "--cache-root", str(self.cache_root)]
        return subprocess.run(
            cmd, cwd=self.project_root, capture_output=True, text=True
        )

    def lua_path(self, rel: str) -> str:
        return (self.test_data / "recipes" / rel).as_posix()

    # ===== ctx.asset =====

    def test_ctx_asset_success(self):
        manifest = self.manifest(
            f"""
packages = {{
  {{
    recipe = "local.ctx_asset_provider@v1",
    source = "{self.lua_path("ctx_asset_provider.lua")}",
  }},
  {{
    recipe = "local.ctx_asset_consumer_ok@v1",
    source = "{self.lua_path("ctx_asset_consumer_ok.lua")}",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_ctx_asset_needed_by_violation(self):
        manifest = self.manifest(
            f"""
packages = {{
  {{
    recipe = "local.ctx_asset_provider@v1",
    source = "{self.lua_path("ctx_asset_provider.lua")}",
  }},
  {{
    recipe = "local.ctx_asset_needed_by_violation@v1",
    source = "{self.lua_path("ctx_asset_needed_by_violation.lua")}",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertNotEqual(result.returncode, 0, "expected needed_by violation to fail")
        self.assertIn("needed_by 'install' but accessed during 'stage'", result.stderr)

    def test_ctx_asset_user_managed_fails(self):
        manifest = self.manifest(
            f"""
packages = {{
  {{
    recipe = "local.ctx_asset_user_provider@v1",
    source = "{self.lua_path("ctx_asset_user_provider.lua")}",
  }},
  {{
    recipe = "local.ctx_asset_user_consumer@v1",
    source = "{self.lua_path("ctx_asset_user_consumer.lua")}",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertNotEqual(result.returncode, 0, "expected user-managed asset access to fail")
        self.assertIn("is user-managed and has no asset path", result.stderr)

    def test_ctx_asset_missing_dependency(self):
        manifest = self.manifest(
            f"""
packages = {{
  {{
    recipe = "local.ctx_asset_missing_dep@v1",
    source = "{self.lua_path("ctx_asset_missing_dep.lua")}",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertNotEqual(result.returncode, 0, "expected missing dependency to fail")
        self.assertIn("has no strong dependency", result.stderr)

    # ===== ctx.product =====

    def test_ctx_product_success(self):
        manifest = self.manifest(
            f"""
packages = {{
  {{
    recipe = "local.product_provider@v1",
    source = "{self.lua_path("product_provider.lua")}",
  }},
  {{
    recipe = "local.ctx_product_consumer_ok@v1",
    source = "{self.lua_path("ctx_product_consumer_ok.lua")}",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_ctx_product_needed_by_violation(self):
        manifest = self.manifest(
            f"""
packages = {{
  {{
    recipe = "local.product_provider@v1",
    source = "{self.lua_path("product_provider.lua")}",
  }},
  {{
    recipe = "local.ctx_product_needed_by_violation@v1",
    source = "{self.lua_path("ctx_product_needed_by_violation.lua")}",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertNotEqual(result.returncode, 0, "expected product needed_by violation to fail")
        self.assertIn("needed_by 'install' but accessed during 'stage'", result.stderr)

    def test_ctx_product_missing_dependency(self):
        manifest = self.manifest(
            f"""
packages = {{
  {{
    recipe = "local.ctx_product_missing_dep@v1",
    source = "{self.lua_path("ctx_product_missing_dep.lua")}",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertNotEqual(result.returncode, 0, "expected missing product dependency to fail")
        self.assertIn("does not declare product dependency", result.stderr)


if __name__ == "__main__":
    unittest.main()


#!/usr/bin/env python3
"""Functional tests for products feature."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from . import test_config


class TestProducts(unittest.TestCase):
    """End-to-end tests for product providers and consumers."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-products-cache-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-products-manifest-"))
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
        cmd = [str(self.envy), *args, "--cache-root", str(self.cache_root)]
        return subprocess.run(
            cmd, cwd=self.project_root, capture_output=True, text=True
        )

    def test_strong_product_dependency_resolves_provider(self):
        manifest = self.manifest(
            f"""
packages = {{
  {{
    recipe = "local.product_consumer_strong@v1",
    source = "{self.lua_path(self.test_data)}/recipes/product_consumer_strong.lua",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)

        provider_dir = self.cache_root / "assets" / "local.product_provider@v1"
        consumer_dir = self.cache_root / "assets" / "local.product_consumer_strong@v1"
        self.assertTrue(provider_dir.exists(), f"missing {provider_dir}")
        self.assertTrue(consumer_dir.exists(), f"missing {consumer_dir}")

    def test_weak_product_dependency_uses_fallback(self):
        manifest = self.manifest(
            f"""
packages = {{
  {{
    recipe = "local.product_consumer_weak@v1",
    source = "{self.lua_path(self.test_data)}/recipes/product_consumer_weak.lua",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)

        provider_dir = self.cache_root / "assets" / "local.product_provider@v1"
        self.assertTrue(provider_dir.exists(), f"missing {provider_dir}")

    def test_missing_product_dependency_errors(self):
        manifest = self.manifest(
            f"""
packages = {{
  {{
    recipe = "local.product_consumer_missing@v1",
    source = "{self.lua_path(self.test_data)}/recipes/product_consumer_missing.lua",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertNotEqual(
            result.returncode, 0, "expected failure for missing product"
        )
        self.assertIn("missing", result.stderr.lower())

    def test_product_collision_errors(self):
        manifest = self.manifest(
            f"""
packages = {{
  {{
    recipe = "local.product_provider@v1",
    source = "{self.lua_path(self.test_data)}/recipes/product_provider.lua",
  }},
  {{
    recipe = "local.product_provider_b@v1",
    source = "{self.lua_path(self.test_data)}/recipes/product_provider_b.lua",
  }},
}}
"""
        )

        result = self.run_envy(["product", "tool", "--manifest", str(manifest)])
        self.assertNotEqual(result.returncode, 0, "collision should fail")
        self.assertIn("Product 'tool'", result.stderr)

    def test_product_command_cached_provider(self):
        manifest = self.manifest(
            f"""
packages = {{
  {{
    recipe = "local.product_provider@v1",
    source = "{self.lua_path(self.test_data)}/recipes/product_provider.lua",
  }},
}}
"""
        )

        result = self.run_envy(["product", "tool", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)
        output = result.stdout.strip()
        self.assertTrue(output.endswith("bin/tool"), f"unexpected output: {output}")
        self.assertIn("local.product_provider@v1", output)

    def test_product_command_programmatic_provider_returns_raw_value(self):
        manifest = self.manifest(
            f"""
packages = {{
  {{
    recipe = "local.product_programmatic@v1",
    source = "{self.lua_path(self.test_data)}/recipes/product_provider_programmatic.lua",
  }},
}}
"""
        )

        result = self.run_envy(["product", "tool", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)
        output = result.stdout.strip()
        self.assertEqual(output, "programmatic-tool")


if __name__ == "__main__":
    unittest.main()

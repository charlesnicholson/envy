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
        cmd = [str(self.envy), "--cache-root", str(self.cache_root), *args]
        return subprocess.run(
            cmd, cwd=self.project_root, capture_output=True, text=True
        )

    def test_strong_product_dependency_resolves_provider(self):
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.product_consumer_strong@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_consumer_strong.lua",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)

        provider_dir = self.cache_root / "packages" / "local.product_provider@v1"
        consumer_dir = self.cache_root / "packages" / "local.product_consumer_strong@v1"
        self.assertTrue(provider_dir.exists(), f"missing {provider_dir}")
        self.assertTrue(consumer_dir.exists(), f"missing {consumer_dir}")

    def test_weak_product_dependency_uses_fallback(self):
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.product_consumer_weak@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_consumer_weak.lua",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)

        provider_dir = self.cache_root / "packages" / "local.product_provider@v1"
        self.assertTrue(provider_dir.exists(), f"missing {provider_dir}")

    def test_missing_product_dependency_errors(self):
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.product_consumer_missing@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_consumer_missing.lua",
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
PACKAGES = {{
  {{
    spec = "local.product_provider@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_provider.lua",
  }},
  {{
    spec = "local.product_provider_b@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_provider_b.lua",
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
PACKAGES = {{
  {{
    spec = "local.product_provider@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_provider.lua",
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
PACKAGES = {{
  {{
    spec = "local.product_programmatic@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_provider_programmatic.lua",
  }},
}}
"""
        )

        result = self.run_envy(["product", "tool", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)
        output = result.stdout.strip()
        self.assertEqual(output, "programmatic-tool")

    def test_product_function_with_options(self):
        """Products can be a function taking options and returning a table."""
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.product_function@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_provider_function.lua",
    options = {{ version = "3.14" }},
  }},
}}
"""
        )

        # Query the dynamically-generated product name
        result = self.run_envy(["product", "python3.14", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)
        output = result.stdout.strip()
        self.assertTrue(output.endswith("bin/python"), f"unexpected output: {output}")

        # Verify second product also works
        result = self.run_envy(["product", "pip3.14", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)
        output = result.stdout.strip()
        self.assertTrue(output.endswith("bin/pip"), f"unexpected output: {output}")

    def test_absolute_path_in_product_value_rejected(self):
        """Product values with absolute paths should be rejected during parsing."""
        lua_content = """
IDENTITY = "local.bad_provider@v1"
PRODUCTS = { tool = "/etc/passwd" }
FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}
INSTALL = function(ctx)
end
"""
        provider_path = self.test_dir / "bad_provider.lua"
        provider_path.write_text(lua_content, encoding="utf-8")

        manifest = self.manifest(
            f"""
PACKAGES = {{{{
  spec = "local.bad_provider@v1",
  source = "{self.lua_path(provider_path)}"
}}}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("absolute", result.stderr.lower())

    def test_path_traversal_in_product_value_rejected(self):
        """Product values with path traversal should be rejected during parsing."""
        lua_content = """
IDENTITY = "local.bad_provider@v1"
PRODUCTS = { tool = "../../etc/passwd" }
FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}
INSTALL = function(ctx)
end
"""
        provider_path = self.test_dir / "bad_provider.lua"
        provider_path.write_text(lua_content, encoding="utf-8")

        manifest = self.manifest(
            f"""
PACKAGES = {{{{
  spec = "local.bad_provider@v1",
  source = "{self.lua_path(provider_path)}"
}}}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("traversal", result.stderr.lower())

    def test_strong_product_dep_not_resolved_as_weak(self):
        """Strong product dependencies should wire directly, not via weak resolution."""
        # Create two providers for same product
        lua_content_a = """
IDENTITY = "local.provider_a@v1"
PRODUCTS = { tool = "bin/tool_a" }
FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}
INSTALL = function(ctx)
end
"""
        provider_a_path = self.test_dir / "provider_a.lua"
        provider_a_path.write_text(lua_content_a, encoding="utf-8")

        lua_content_b = """
IDENTITY = "local.provider_b@v1"
PRODUCTS = { other_tool = "bin/other" }
FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}
INSTALL = function(ctx)
end
"""
        provider_b_path = self.test_dir / "provider_b.lua"
        provider_b_path.write_text(lua_content_b, encoding="utf-8")

        # Consumer with STRONG dep on provider_a (has source)
        # If this goes through weak resolution, it might pick up provider_b instead
        lua_content_consumer = f"""
IDENTITY = "local.consumer_strong_only@v1"
DEPENDENCIES = {{
  {{
    product = "tool",
    spec = "local.provider_a@v1",
    source = "{self.lua_path(provider_a_path)}",
  }},
}}
FETCH = {{
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}}
INSTALL = function(ctx)
  local tool_path = envy.asset("local.provider_a@v1")
  if not tool_path:match("provider_a") then
    error("Expected provider_a but got: " .. tool_path)
  end
end
"""
        consumer_path = self.test_dir / "consumer_strong.lua"
        consumer_path.write_text(lua_content_consumer, encoding="utf-8")

        # Include both providers in manifest - provider_b appears first (registry order)
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.provider_b@v1",
    source = "{self.lua_path(provider_b_path)}"
  }},
  {{
    spec = "local.consumer_strong_only@v1",
    source = "{self.lua_path(consumer_path)}"
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)

        # Verify consumer used provider_a (from strong dep), not provider_b (from registry)
        provider_a_dir = self.cache_root / "packages" / "local.provider_a@v1"
        self.assertTrue(provider_a_dir.exists(), "provider_a should be fetched")

    def test_product_semantic_cycle_detected(self):
        """Product dependencies forming a semantic cycle should be detected and rejected."""
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.cycle_a@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_cycle_a.lua",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertNotEqual(result.returncode, 0)
        # Should detect cycle through products
        self.assertTrue(
            "cycle" in result.stderr.lower() or "circular" in result.stderr.lower(),
            f"Expected cycle error but got: {result.stderr}",
        )

    def test_ref_only_product_dependency_unconstrained(self):
        """Ref-only product dependencies (no recipe/source) should resolve to any provider."""
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.product_provider@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_provider.lua",
  }},
  {{
    spec = "local.ref_only_consumer@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_ref_only_consumer.lua",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)

        # Both provider and consumer should exist
        provider_dir = self.cache_root / "packages" / "local.product_provider@v1"
        consumer_dir = self.cache_root / "packages" / "local.ref_only_consumer@v1"
        self.assertTrue(provider_dir.exists(), f"missing {provider_dir}")
        self.assertTrue(consumer_dir.exists(), f"missing {consumer_dir}")

    def test_ref_only_product_dependency_missing_errors(self):
        """Ref-only product dependency with no provider should error."""
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.ref_only_consumer@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_ref_only_consumer.lua",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--manifest", str(manifest)])
        self.assertNotEqual(result.returncode, 0)
        # Should report missing product
        self.assertTrue(
            "tool" in result.stderr.lower() and "not found" in result.stderr.lower(),
            f"Expected missing product error but got: {result.stderr}",
        )

    def test_product_listing_shows_all_products(self):
        """Product command with no args should list all products from all providers."""
        # Create second provider with non-colliding product
        lua_content = """
IDENTITY = "local.list_provider@v1"
PRODUCTS = { compiler = "bin/gcc" }
FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}
INSTALL = function(ctx)
end
"""
        list_provider_path = self.test_dir / "list_provider.lua"
        list_provider_path.write_text(lua_content, encoding="utf-8")

        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.product_provider@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_provider.lua",
  }},
  {{
    spec = "local.list_provider@v1",
    source = "{self.lua_path(list_provider_path)}",
  }},
}}
"""
        )

        result = self.run_envy(["product", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)
        # Should see products from both providers in stderr (human-readable)
        # product_provider has "tool", list_provider has "compiler"
        self.assertIn("tool", result.stderr.lower())
        self.assertIn("compiler", result.stderr.lower())

    def test_product_listing_json_output(self):
        """Product command with --json should output JSON array to stdout."""
        import json

        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.product_provider@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_provider.lua",
  }},
}}
"""
        )

        result = self.run_envy(["product", "--json", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)

        # Parse JSON from stdout
        products = json.loads(result.stdout)
        self.assertIsInstance(products, list)
        self.assertGreater(len(products), 0)

        # Find the tool product
        tool_product = next((p for p in products if p["product"] == "tool"), None)
        assert tool_product
        self.assertIsNotNone(tool_product, "tool product not found in JSON output")
        self.assertEqual(tool_product["value"], "bin/tool")
        self.assertEqual(tool_product["provider"], "local.product_provider@v1")
        self.assertFalse(tool_product["user_managed"])

    def test_product_listing_programmatic_marked(self):
        """Product listing should mark programmatic products."""
        import json

        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.product_programmatic@v1",
    source = "{self.lua_path(self.test_data)}/specs/product_provider_programmatic.lua",
  }},
}}
"""
        )

        result = self.run_envy(["product", "--json", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)

        products = json.loads(result.stdout)
        tool_product = next((p for p in products if p["product"] == "tool"), None)
        assert tool_product
        self.assertIsNotNone(tool_product)
        self.assertTrue(tool_product["user_managed"])
        self.assertEqual(tool_product["pkg_path"], "")

    def test_product_listing_empty(self):
        """Product listing with no products should indicate empty result."""
        # Create manifest with recipe that has no products
        lua_content = """
IDENTITY = "local.no_products@v1"
FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}
INSTALL = function(ctx)
end
"""
        no_products_path = self.test_dir / "no_products.lua"
        no_products_path.write_text(lua_content, encoding="utf-8")

        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.no_products@v1",
    source = "{self.lua_path(no_products_path)}"
  }},
}}
"""
        )

        result = self.run_envy(["product", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("no products", result.stderr.lower())

    def test_product_command_resolves_providers_clean_cache(self):
        """Product command must resolve graph to find providers on clean cache.

        Regression test: product command should call resolve_graph() with all
        manifest packages to find product providers, not just recipes directly
        requested. This ensures products can be queried even when the provider
        isn't explicitly the target.
        """
        # Create product provider recipe
        provider_recipe = """IDENTITY = "local.test_product_query_provider@v1"

FETCH = { source = "test_data/archives/test.tar.gz",
          sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c" }

INSTALL = function(ctx)
end

PRODUCTS = { test_query_tool = "bin/query_tool" }
"""
        provider_path = self.test_dir / "test_product_query_provider.lua"
        provider_path.write_text(provider_recipe, encoding="utf-8")

        # Manifest with the provider
        manifest = self.manifest(
            f"""
PACKAGES = {{
    {{ spec = "local.test_product_query_provider@v1", source = "{self.lua_path(provider_path)}" }}
}}
"""
        )

        # Query the product with clean cache - should resolve graph and find provider
        result = self.run_envy(
            ["product", "test_query_tool", "--manifest", str(manifest)]
        )

        # Should succeed - product command must resolve graph to find provider
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertTrue(result.stdout.strip(), "Expected product value in stdout")

        # Verify the output contains the expected path
        output = result.stdout.strip()
        self.assertIn(
            "bin/query_tool", output, f"Expected product path in output: {output}"
        )
        self.assertIn(
            "local.test_product_query_provider@v1",
            output,
            f"Expected provider identity in output: {output}",
        )


if __name__ == "__main__":
    unittest.main()

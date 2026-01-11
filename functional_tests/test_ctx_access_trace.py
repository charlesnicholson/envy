"""Functional test to validate ctx.package/ctx.product trace emission and ordering."""

import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from . import test_config

# =============================================================================
# Shared provider specs
# =============================================================================

# Dependency library that provides a simple file
SPEC_DEP_VAL_LIB = """IDENTITY = "local.dep_val_lib@v1"

function FETCH(tmp_dir, options)
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.run("echo 'lib built' > " .. install_dir .. "/lib.txt", { quiet = true })
end
"""

# Product provider exposing 'tool' product at bin/tool
SPEC_PRODUCT_PROVIDER = """IDENTITY = "local.product_provider@v1"
PRODUCTS = { tool = "bin/tool" }

function FETCH(tmp_dir, options)
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.run("mkdir -p " .. install_dir .. "/bin && echo 'tool' > " .. install_dir .. "/bin/tool", { quiet = true })
end
"""


class TestCtxAccessTrace(unittest.TestCase):
    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-ctx-trace-cache-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-ctx-trace-specs-"))
        self.envy = test_config.get_envy_executable()

        # Write shared provider specs
        (self.test_dir / "dep_val_lib.lua").write_text(SPEC_DEP_VAL_LIB, encoding="utf-8")
        (self.test_dir / "product_provider.lua").write_text(SPEC_PRODUCT_PROVIDER, encoding="utf-8")

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def load_trace(self, trace_file: Path):
        events = []
        with trace_file.open("r", encoding="utf-8") as f:
            for line in f:
                events.append(json.loads(line))
        return events

    def test_ctx_access_trace_emitted(self):
        """ctx.package/ctx.product emit trace events with allow/deny status."""
        # Main spec: tests allowed and denied access to package and product APIs
        spec_trace_ctx_access = """IDENTITY = "local.trace_ctx_access@v1"

DEPENDENCIES = {
  { spec = "local.dep_val_lib@v1", source = "dep_val_lib.lua", needed_by = "install" },
  { product = "tool", spec = "local.product_provider@v1", source = "product_provider.lua", needed_by = "install" },
}

function FETCH(tmp_dir, options)
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Allowed package access
  envy.package("local.dep_val_lib@v1")

  -- Denied package access (undeclared)
  local ok, err = pcall(function() envy.package("local.missing@v1") end)
  assert(not ok, "expected missing package access to fail")

  -- Allowed product access
  envy.product("tool")

  -- Denied product access (undeclared)
  local ok2, err2 = pcall(function() envy.product("missing_prod") end)
  assert(not ok2, "expected missing product access to fail")
end
"""
        (self.test_dir / "trace_ctx_access.lua").write_text(spec_trace_ctx_access, encoding="utf-8")

        trace_file = self.cache_root / "trace.jsonl"
        result = subprocess.run(
            [
                str(self.envy),
                f"--cache-root={self.cache_root}",
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.trace_ctx_access@v1",
                str(self.test_dir / "trace_ctx_access.lua"),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertTrue(trace_file.exists(), "Trace file not created")

        events = self.load_trace(trace_file)
        package_events = [
            e for e in events if e.get("event") == "lua_ctx_package_access"
        ]
        product_events = [
            e for e in events if e.get("event") == "lua_ctx_product_access"
        ]

        self.assertGreaterEqual(
            len(package_events), 2, "expected allowed+denied package events"
        )
        self.assertGreaterEqual(
            len(product_events), 2, "expected allowed+denied product events"
        )

        # Check allow/deny flags and phases
        allowed_packages = [e for e in package_events if e.get("allowed") is True]
        denied_packages = [e for e in package_events if e.get("allowed") is False]
        self.assertTrue(
            any("dep_val_lib" in e.get("target", "") for e in allowed_packages)
        )
        self.assertTrue(any("missing" in e.get("target", "") for e in denied_packages))

        allowed_products = [e for e in product_events if e.get("allowed") is True]
        denied_products = [e for e in product_events if e.get("allowed") is False]
        self.assertTrue(any(e.get("product") == "tool" for e in allowed_products))
        self.assertTrue(
            any(e.get("product") == "missing_prod" for e in denied_products)
        )

        # Verify chronological ordering: allowed package should appear before denied package (same phase)
        package_indices = {e["target"]: i for i, e in enumerate(package_events)}
        self.assertLess(
            package_indices.get("local.dep_val_lib@v1", 9999),
            package_indices.get("local.missing@v1", 9999),
        )


if __name__ == "__main__":
    unittest.main()

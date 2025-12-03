#!/usr/bin/env python3
"""Functional test to validate ctx.asset/ctx.product trace emission and ordering."""

import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from . import test_config


class TestCtxAccessTrace(unittest.TestCase):
    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-ctx-trace-cache-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent

    def tearDown(self):
        import shutil

        shutil.rmtree(self.cache_root, ignore_errors=True)

    def run_envy(self, trace_file: Path):
        return subprocess.run(
            [
                str(self.envy),
                f"--trace=file:{trace_file}",
                "engine-test",
                "local.trace_ctx_access@v1",
                "test_data/recipes/trace_ctx_access.lua",
                f"--cache-root={self.cache_root}",
            ],
            cwd=self.project_root,
            capture_output=True,
            text=True,
        )

    def load_trace(self, trace_file: Path):
        events = []
        with trace_file.open("r", encoding="utf-8") as f:
            for line in f:
                events.append(json.loads(line))
        return events

    def test_ctx_access_trace_emitted(self):
        trace_file = self.cache_root / "trace.jsonl"
        result = self.run_envy(trace_file)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertTrue(trace_file.exists(), "Trace file not created")

        events = self.load_trace(trace_file)
        asset_events = [e for e in events if e.get("event") == "lua_ctx_asset_access"]
        product_events = [e for e in events if e.get("event") == "lua_ctx_product_access"]

        self.assertGreaterEqual(len(asset_events), 2, "expected allowed+denied asset events")
        self.assertGreaterEqual(len(product_events), 2, "expected allowed+denied product events")

        # Check allow/deny flags and phases
        allowed_assets = [e for e in asset_events if e.get("allowed") is True]
        denied_assets = [e for e in asset_events if e.get("allowed") is False]
        self.assertTrue(any("dep_val_lib" in e.get("target", "") for e in allowed_assets))
        self.assertTrue(any("missing" in e.get("target", "") for e in denied_assets))

        allowed_products = [e for e in product_events if e.get("allowed") is True]
        denied_products = [e for e in product_events if e.get("allowed") is False]
        self.assertTrue(any(e.get("product") == "tool" for e in allowed_products))
        self.assertTrue(any(e.get("product") == "missing_prod" for e in denied_products))

        # Verify chronological ordering: allowed asset should appear before denied asset (same phase)
        asset_indices = {e["target"]: i for i, e in enumerate(asset_events)}
        self.assertLess(asset_indices.get("local.dep_val_lib@v1", 9999), asset_indices.get("local.missing@v1", 9999))


if __name__ == "__main__":
    unittest.main()

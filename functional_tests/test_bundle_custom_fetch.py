#!/usr/bin/env python3
"""Functional tests for bundle custom fetch functionality.

Tests bundles with custom fetch functions, including bundles
that have dependencies that must be installed before the fetch runs.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from . import test_config


class TestBundleCustomFetch(unittest.TestCase):
    """Tests for bundles with custom fetch functions."""

    def run_envy(self, args: list[str], cwd: str = None) -> subprocess.CompletedProcess:
        """Run envy with given arguments."""
        exe = test_config.get_envy_executable()
        env = test_config.get_test_env()
        return subprocess.run(
            [str(exe)] + args,
            cwd=cwd,
            capture_output=True,
            text=True,
            env=env
        )

    def test_bundle_custom_fetch_simple(self):
        """Test bundle with simple custom fetch function (no dependencies)."""
        with tempfile.TemporaryDirectory() as tmpdir:
            bundle_dir = os.path.join(tmpdir, "bundle")
            os.makedirs(os.path.join(bundle_dir, "specs"))

            # Create bundle manifest
            bundle_lua = """
BUNDLE = "test.custom-fetch-bundle@v1"
SPECS = {
    ["test.simple@v1"] = "specs/simple.lua"
}
"""
            with open(os.path.join(bundle_dir, "envy-bundle.lua"), "w") as f:
                f.write(bundle_lua)

            # Create simple spec
            simple_spec = """
IDENTITY = "test.simple@v1"
CHECK = function(project_root) return true end
INSTALL = function() end
"""
            with open(os.path.join(bundle_dir, "specs", "simple.lua"), "w") as f:
                f.write(simple_spec)

            # Escape path for Lua string
            bundle_dir_escaped = bundle_dir.replace("\\", "\\\\")

            # Create manifest with custom fetch bundle
            manifest = f"""
-- @envy bin "tools"

BUNDLES = {{
    ["custom"] = {{
        identity = "test.custom-fetch-bundle@v1",
        source = {{
            fetch = function(tmp_dir)
                -- Simple fetch: copy bundle files
                envy.run("cp -r '{bundle_dir_escaped}/'* " .. tmp_dir .. "/")
                envy.commit_fetch({{"envy-bundle.lua", "specs"}})
            end,
            dependencies = {{}}
        }}
    }}
}}

PACKAGES = {{
    {{spec = "test.simple@v1", bundle = "custom"}}
}}
"""
            manifest_path = os.path.join(tmpdir, "envy.lua")
            with open(manifest_path, "w") as f:
                f.write(manifest)

            # Run sync
            result = self.run_envy(["sync", "--install-all", "--manifest", manifest_path], tmpdir)

            # Should succeed - bundle custom fetch creates the bundle, then spec resolves
            self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")

    def test_bundle_custom_fetch_with_dependency(self):
        """Test bundle custom fetch with user-managed dependency."""
        with tempfile.TemporaryDirectory() as tmpdir:
            specs_dir = os.path.join(tmpdir, "specs")
            os.makedirs(specs_dir)

            # Create a user-managed tool spec that the bundle depends on
            tool_spec = """
IDENTITY = "local.fetcher-tool@v1"
CHECK = function(project_root)
    return true  -- Always installed
end
INSTALL = function()
    -- Tool installs (user-managed)
end
"""
            tool_path = os.path.join(specs_dir, "fetcher-tool.lua")
            with open(tool_path, "w") as f:
                f.write(tool_spec)

            # Create bundle directory
            bundle_dir = os.path.join(tmpdir, "bundle")
            os.makedirs(os.path.join(bundle_dir, "specs"))

            bundle_lua = """
BUNDLE = "test.dep-bundle@v1"
SPECS = {
    ["test.from-dep-bundle@v1"] = "specs/from-dep.lua"
}
"""
            with open(os.path.join(bundle_dir, "envy-bundle.lua"), "w") as f:
                f.write(bundle_lua)

            from_dep_spec = """
IDENTITY = "test.from-dep-bundle@v1"
CHECK = function(project_root) return true end
INSTALL = function() end
"""
            with open(os.path.join(bundle_dir, "specs", "from-dep.lua"), "w") as f:
                f.write(from_dep_spec)

            # Escape paths for Lua strings
            bundle_dir_escaped = bundle_dir.replace("\\", "\\\\")
            tool_path_escaped = tool_path.replace("\\", "\\\\")

            # Create manifest with custom fetch bundle that has a dependency
            # Note: We don't use envy.package() since the tool is user-managed
            # The key test is that dependencies are resolved before the fetch runs
            manifest = f"""
-- @envy bin "tools"

BUNDLES = {{
    ["dep-bundle"] = {{
        identity = "test.dep-bundle@v1",
        source = {{
            fetch = function(tmp_dir)
                -- Dependencies are resolved before this runs (user-managed tool installed)
                -- Copy bundle files
                envy.run("cp -r '{bundle_dir_escaped}/'* " .. tmp_dir .. "/")
                envy.commit_fetch({{"envy-bundle.lua", "specs"}})
            end,
            dependencies = {{
                {{spec = "local.fetcher-tool@v1", source = "{tool_path_escaped}"}}
            }}
        }}
    }}
}}

PACKAGES = {{
    {{spec = "test.from-dep-bundle@v1", bundle = "dep-bundle"}}
}}
"""
            manifest_path = os.path.join(tmpdir, "envy.lua")
            with open(manifest_path, "w") as f:
                f.write(manifest)

            # Run sync
            result = self.run_envy(["sync", "--install-all", "--manifest", manifest_path], tmpdir)

            # Should succeed - dependency installs first, then custom fetch runs
            self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")


if __name__ == "__main__":
    unittest.main()

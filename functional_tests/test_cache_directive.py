import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from . import test_config


class TestCacheDirective(unittest.TestCase):
    """Tests for '-- @envy cache' manifest directive."""

    def setUp(self):
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-cache-directive-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent
        self.test_data = self.project_root / "test_data"

    def tearDown(self):
        shutil.rmtree(self.test_dir, ignore_errors=True)

    @staticmethod
    def lua_path(path: Path) -> str:
        """Convert path to Lua-safe string (forward slashes work on all platforms)."""
        return path.as_posix()

    def create_manifest(self, content: str) -> Path:
        """Create manifest file with given content."""
        manifest_path = self.test_dir / "envy.lua"
        manifest_path.write_text(content, encoding="utf-8")
        return manifest_path

    def run_sync(
        self,
        manifest: Path,
        cache_root: str | None = None,
        env_override: dict | None = None,
    ):
        """Run 'envy sync' command and return result."""
        cmd = [str(self.envy)]
        if cache_root:
            cmd.extend(["--cache-root", cache_root])
        cmd.extend(["sync", "--manifest", str(manifest)])

        env = test_config.get_test_env()
        if env_override:
            env.update(env_override)

        result = subprocess.run(
            cmd,
            cwd=self.project_root,
            capture_output=True,
            text=True,
            env=env,
        )
        return result

    def test_cache_directive_tilde_expansion(self):
        """Cache directive with ~ expands to home directory."""
        # Create a custom cache path using ~
        custom_cache_name = f".envy-cache-test-tilde-{os.getpid()}"
        home = Path.home()
        expected_cache = home / custom_cache_name

        try:
            # Use build_dependency.lua which is cache-managed (has FETCH, STAGE, BUILD)
            # Note: @envy directive values must be quoted
            manifest = self.create_manifest(
                f"""-- @envy cache "~/{custom_cache_name}"
PACKAGES = {{
    {{ recipe = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/recipes/build_dependency.lua" }},
}}
"""
            )

            # Remove any pre-existing cache
            if expected_cache.exists():
                shutil.rmtree(expected_cache)

            # Clear ENVY_CACHE_ROOT to ensure manifest directive is used
            env = test_config.get_test_env()
            env.pop("ENVY_CACHE_ROOT", None)

            result = self.run_sync(manifest, env_override=env)

            self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
            self.assertTrue(
                expected_cache.exists(),
                f"Cache should exist at {expected_cache}. stderr: {result.stderr}",
            )
            # Verify the package was installed in the custom cache
            asset_path = expected_cache / "assets" / "local.build_dependency@v1"
            self.assertTrue(
                asset_path.exists(),
                f"Asset should exist at {asset_path}",
            )
        finally:
            # Clean up the custom cache
            if expected_cache.exists():
                shutil.rmtree(expected_cache, ignore_errors=True)

    def test_cache_directive_env_var_expansion(self):
        """Cache directive with $HOME expands environment variable."""
        # Create a custom cache path using $HOME
        custom_cache_name = f".envy-cache-test-env-{os.getpid()}"
        home = Path.home()
        expected_cache = home / custom_cache_name

        try:
            # Use build_dependency.lua which is cache-managed
            # Note: @envy directive values must be quoted
            manifest = self.create_manifest(
                f"""-- @envy cache "$HOME/{custom_cache_name}"
PACKAGES = {{
    {{ recipe = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/recipes/build_dependency.lua" }},
}}
"""
            )

            # Remove any pre-existing cache
            if expected_cache.exists():
                shutil.rmtree(expected_cache)

            # Clear ENVY_CACHE_ROOT to ensure manifest directive is used
            env = test_config.get_test_env()
            env.pop("ENVY_CACHE_ROOT", None)

            result = self.run_sync(manifest, env_override=env)

            self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
            self.assertTrue(
                expected_cache.exists(),
                f"Cache should exist at {expected_cache}. stderr: {result.stderr}",
            )
        finally:
            # Clean up the custom cache
            if expected_cache.exists():
                shutil.rmtree(expected_cache, ignore_errors=True)

    def test_cli_cache_root_overrides_manifest(self):
        """CLI --cache-root takes precedence over manifest cache directive."""
        cli_cache = self.test_dir / "cli-cache"
        manifest_cache_name = f".envy-cache-test-override-{os.getpid()}"
        manifest_cache = Path.home() / manifest_cache_name

        try:
            # Use build_dependency.lua which is cache-managed
            # Note: @envy directive values must be quoted
            manifest = self.create_manifest(
                f"""-- @envy cache "~/{manifest_cache_name}"
PACKAGES = {{
    {{ recipe = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/recipes/build_dependency.lua" }},
}}
"""
            )

            # Remove any pre-existing caches
            if manifest_cache.exists():
                shutil.rmtree(manifest_cache)

            # Clear ENVY_CACHE_ROOT to isolate CLI vs manifest behavior
            env = test_config.get_test_env()
            env.pop("ENVY_CACHE_ROOT", None)

            result = self.run_sync(
                manifest, cache_root=str(cli_cache), env_override=env
            )

            self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
            # CLI cache should be used
            self.assertTrue(
                cli_cache.exists(),
                f"CLI cache should exist at {cli_cache}",
            )
            # Manifest cache should NOT be created
            self.assertFalse(
                manifest_cache.exists(),
                f"Manifest cache should NOT exist at {manifest_cache}",
            )
        finally:
            if manifest_cache.exists():
                shutil.rmtree(manifest_cache, ignore_errors=True)

    def test_env_cache_root_overrides_manifest(self):
        """ENVY_CACHE_ROOT env takes precedence over manifest cache directive."""
        env_cache = self.test_dir / "env-cache"
        manifest_cache_name = f".envy-cache-test-env-override-{os.getpid()}"
        manifest_cache = Path.home() / manifest_cache_name

        try:
            # Use build_dependency.lua which is cache-managed
            # Note: @envy directive values must be quoted
            manifest = self.create_manifest(
                f"""-- @envy cache "~/{manifest_cache_name}"
PACKAGES = {{
    {{ recipe = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/recipes/build_dependency.lua" }},
}}
"""
            )

            # Remove any pre-existing caches
            if manifest_cache.exists():
                shutil.rmtree(manifest_cache)

            # Set ENVY_CACHE_ROOT to override manifest directive
            env = test_config.get_test_env()
            env["ENVY_CACHE_ROOT"] = str(env_cache)

            result = self.run_sync(manifest, env_override=env)

            self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
            # Env cache should be used
            self.assertTrue(
                env_cache.exists(),
                f"Env cache should exist at {env_cache}",
            )
            # Manifest cache should NOT be created
            self.assertFalse(
                manifest_cache.exists(),
                f"Manifest cache should NOT exist at {manifest_cache}",
            )
        finally:
            if manifest_cache.exists():
                shutil.rmtree(manifest_cache, ignore_errors=True)

    def test_cache_directive_plain_path(self):
        """Cache directive with plain path uses it directly."""
        custom_cache = self.test_dir / "custom-cache"

        # Use build_dependency.lua which is cache-managed
        # Note: @envy directive values must be quoted
        manifest = self.create_manifest(
            f"""-- @envy cache "{self.lua_path(custom_cache)}"
PACKAGES = {{
    {{ recipe = "local.build_dependency@v1", source = "{self.lua_path(self.test_data)}/recipes/build_dependency.lua" }},
}}
"""
        )

        # Clear ENVY_CACHE_ROOT to ensure manifest directive is used
        env = test_config.get_test_env()
        env.pop("ENVY_CACHE_ROOT", None)

        result = self.run_sync(manifest, env_override=env)

        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertTrue(
            custom_cache.exists(),
            f"Cache should exist at {custom_cache}",
        )
        # Verify the package was installed
        asset_path = custom_cache / "assets" / "local.build_dependency@v1"
        self.assertTrue(
            asset_path.exists(),
            f"Asset should exist at {asset_path}",
        )


if __name__ == "__main__":
    unittest.main()

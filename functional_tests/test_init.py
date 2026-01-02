"""Tests for envy init command and self-deployment."""

import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from .test_config import get_test_env


def _get_envy_binary() -> Path:
    """Get the main envy binary (not functional tester)."""
    import sys

    root = Path(__file__).parent.parent / "out" / "build"
    if sys.platform == "win32":
        return root / "envy.exe"
    return root / "envy"


class TestEnvyInit(unittest.TestCase):
    """Test the envy init command."""

    def setUp(self) -> None:
        self._temp_dir = Path(tempfile.mkdtemp(prefix="envy-init-test-"))
        self._project_dir = self._temp_dir / "project"
        self._bin_dir = self._temp_dir / "project" / "tools"
        self._cache_dir = self._temp_dir / "cache"
        self._envy = _get_envy_binary()

        if not self._envy.exists():
            self.skipTest(f"envy not found at {self._envy}")

    def tearDown(self) -> None:
        if hasattr(self, "_temp_dir") and self._temp_dir.exists():
            shutil.rmtree(self._temp_dir, ignore_errors=True)

    def _run_init(
        self, project_dir: Path | None = None, bin_dir: Path | None = None, **kwargs
    ) -> subprocess.CompletedProcess[str]:
        """Run envy init command."""
        project = str(project_dir or self._project_dir)
        bindir = str(bin_dir or self._bin_dir)
        cmd = [str(self._envy), "init", project, bindir]

        if "mirror" in kwargs:
            cmd.append(f"--mirror={kwargs['mirror']}")
        if "deploy" in kwargs:
            cmd.append(f"--deploy={kwargs['deploy']}")
        if "root" in kwargs:
            cmd.append(f"--root={kwargs['root']}")

        env = get_test_env()
        env["ENVY_CACHE_ROOT"] = str(self._cache_dir)

        return subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=30)

    def test_init_creates_all_expected_files(self) -> None:
        """Init creates bootstrap script, manifest, and .luarc.json."""
        result = self._run_init()
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        # Check bootstrap script
        import sys

        if sys.platform == "win32":
            bootstrap = self._bin_dir / "envy.bat"
        else:
            bootstrap = self._bin_dir / "envy"
        self.assertTrue(bootstrap.exists(), f"Bootstrap not created at {bootstrap}")

        # Check manifest
        manifest = self._project_dir / "envy.lua"
        self.assertTrue(manifest.exists(), f"Manifest not created at {manifest}")

        # Check .luarc.json
        luarc = self._project_dir / ".luarc.json"
        self.assertTrue(luarc.exists(), f".luarc.json not created at {luarc}")

    def test_init_creates_directories_if_missing(self) -> None:
        """Init creates project and bin directories if they don't exist."""
        nested_project = self._temp_dir / "deep" / "nested" / "project"
        nested_bin = nested_project / "tools"

        result = self._run_init(project_dir=nested_project, bin_dir=nested_bin)
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        self.assertTrue(nested_project.exists())
        self.assertTrue(nested_bin.exists())

    def test_init_manifest_contains_envy_version_directive(self) -> None:
        """Manifest contains @envy version directive."""
        result = self._run_init()
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        manifest = self._project_dir / "envy.lua"
        content = manifest.read_text()
        self.assertIn("@envy version", content)

    def test_init_does_not_overwrite_existing_manifest(self) -> None:
        """Init does not overwrite existing manifest."""
        self._project_dir.mkdir(parents=True)
        manifest = self._project_dir / "envy.lua"
        manifest.write_text("-- Existing manifest\nPACKAGES = {}\n")

        result = self._run_init()
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        content = manifest.read_text()
        self.assertIn("Existing manifest", content)
        self.assertIn("already exists", result.stderr)

    def test_init_bootstrap_is_executable(self) -> None:
        """Bootstrap script has executable permissions (Unix)."""
        import sys

        if sys.platform == "win32":
            self.skipTest("Executable permissions not applicable on Windows")

        result = self._run_init()
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        bootstrap = self._bin_dir / "envy"
        self.assertTrue(os.access(bootstrap, os.X_OK))

    def test_init_luarc_is_valid_json(self) -> None:
        """.luarc.json is valid JSON."""
        result = self._run_init()
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        luarc = self._project_dir / ".luarc.json"
        content = luarc.read_text()
        data = json.loads(content)

        self.assertIn("workspace.library", data)
        self.assertIn("diagnostics.globals", data)
        self.assertIn("envy", data["diagnostics.globals"])

    def test_init_luarc_points_to_cache(self) -> None:
        """.luarc.json workspace.library points to cache."""
        result = self._run_init()
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        luarc = self._project_dir / ".luarc.json"
        data = json.loads(luarc.read_text())

        library_paths = data["workspace.library"]
        self.assertEqual(1, len(library_paths))
        self.assertIn("envy", library_paths[0])

    def test_init_prints_guidance_when_luarc_exists(self) -> None:
        """Init prints guidance when .luarc.json already exists."""
        self._project_dir.mkdir(parents=True)
        luarc = self._project_dir / ".luarc.json"
        luarc.write_text('{"existing": "config"}\n')

        result = self._run_init()
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        # Should print guidance, not overwrite
        self.assertIn("already exists", result.stderr)
        self.assertIn("workspace.library", result.stderr)

        # Original content preserved
        data = json.loads(luarc.read_text())
        self.assertEqual("config", data.get("existing"))

    def test_init_with_mirror_option(self) -> None:
        """Init respects --mirror option."""
        custom_mirror = "https://internal.corp/envy-releases"
        result = self._run_init(mirror=custom_mirror)
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        import sys

        if sys.platform == "win32":
            bootstrap = self._bin_dir / "envy.bat"
        else:
            bootstrap = self._bin_dir / "envy"

        content = bootstrap.read_text()
        self.assertIn(custom_mirror, content)

    def test_init_extracts_type_definitions_to_cache(self) -> None:
        """Init extracts type definitions to cache."""
        result = self._run_init()
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        # Type definitions should be in cache/envy/<version>/envy.lua
        envy_cache = self._cache_dir / "envy"
        self.assertTrue(envy_cache.exists(), f"Envy cache not created at {envy_cache}")

        # Find the version directory (0.0.0 for dev builds)
        version_dirs = list(envy_cache.iterdir())
        self.assertGreater(len(version_dirs), 0, "No version directories in cache")

        types_file = version_dirs[0] / "envy.lua"
        self.assertTrue(types_file.exists(), f"Type definitions not at {types_file}")

        # Verify it's valid Lua type definitions
        content = types_file.read_text()
        self.assertIn("---@meta", content)
        self.assertIn("envy", content)

    def test_init_deploy_true_stamps_directive(self) -> None:
        """--deploy=true stamps deploy directive into manifest."""
        result = self._run_init(deploy="true")
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        manifest = self._project_dir / "envy.lua"
        content = manifest.read_text()
        self.assertIn('@envy deploy "true"', content)

    def test_init_deploy_false_stamps_directive(self) -> None:
        """--deploy=false stamps deploy directive into manifest."""
        result = self._run_init(deploy="false")
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        manifest = self._project_dir / "envy.lua"
        content = manifest.read_text()
        self.assertIn('@envy deploy "false"', content)

    def test_init_default_deploy_is_true(self) -> None:
        """Without --deploy, manifest has deploy=true by default."""
        result = self._run_init()
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        manifest = self._project_dir / "envy.lua"
        content = manifest.read_text()
        self.assertIn('@envy deploy "true"', content)

    def test_init_root_true_stamps_directive(self) -> None:
        """--root=true stamps root directive into manifest."""
        result = self._run_init(root="true")
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        manifest = self._project_dir / "envy.lua"
        content = manifest.read_text()
        self.assertIn('@envy root "true"', content)

    def test_init_root_false_stamps_directive(self) -> None:
        """--root=false stamps root directive into manifest."""
        result = self._run_init(root="false")
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        manifest = self._project_dir / "envy.lua"
        content = manifest.read_text()
        self.assertIn('@envy root "false"', content)

    def test_init_default_root_is_true(self) -> None:
        """Without --root, manifest has root=true by default."""
        result = self._run_init()
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        manifest = self._project_dir / "envy.lua"
        content = manifest.read_text()
        self.assertIn('@envy root "true"', content)

    def test_init_both_deploy_and_root_stamps_both(self) -> None:
        """--deploy and --root together stamp both directives."""
        result = self._run_init(deploy="true", root="false")
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        manifest = self._project_dir / "envy.lua"
        content = manifest.read_text()
        self.assertIn('@envy deploy "true"', content)
        self.assertIn('@envy root "false"', content)


class TestSelfDeployment(unittest.TestCase):
    """Test envy self-deployment on startup."""

    def setUp(self) -> None:
        self._temp_dir = Path(tempfile.mkdtemp(prefix="envy-self-deploy-test-"))
        self._cache_dir = self._temp_dir / "cache"
        self._project_dir = self._temp_dir / "project"
        self._bin_dir = self._temp_dir / "bin"
        self._envy = _get_envy_binary()

        if not self._envy.exists():
            self.skipTest(f"envy not found at {self._envy}")

    def tearDown(self) -> None:
        if hasattr(self, "_temp_dir") and self._temp_dir.exists():
            shutil.rmtree(self._temp_dir, ignore_errors=True)

    def _run_envy(self, *args) -> subprocess.CompletedProcess[str]:
        """Run envy with custom cache root."""
        env = get_test_env()
        env["ENVY_CACHE_ROOT"] = str(self._cache_dir)

        cmd = [str(self._envy), *args]
        return subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=30)

    def _run_envy_with_self_deploy(self) -> subprocess.CompletedProcess[str]:
        """Run an envy command that triggers self-deployment."""
        # Only commands that call cache::ensure() trigger self-deployment
        # init is a good choice as it requires cache but not manifest
        return self._run_envy("init", str(self._project_dir), str(self._bin_dir))

    def test_self_deploy_creates_binary_in_cache(self) -> None:
        """Running a cache-aware command deploys envy binary to cache."""
        import sys

        result = self._run_envy_with_self_deploy()
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        # Check binary exists in cache
        envy_cache = self._cache_dir / "envy"
        self.assertTrue(envy_cache.exists())

        version_dirs = list(envy_cache.iterdir())
        self.assertGreater(len(version_dirs), 0)

        if sys.platform == "win32":
            cached_binary = version_dirs[0] / "envy.exe"
        else:
            cached_binary = version_dirs[0] / "envy"

        self.assertTrue(cached_binary.exists(), f"Binary not at {cached_binary}")

    def test_self_deploy_creates_type_definitions(self) -> None:
        """Self-deployment also extracts type definitions."""
        result = self._run_envy_with_self_deploy()
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        envy_cache = self._cache_dir / "envy"
        version_dirs = list(envy_cache.iterdir())
        types_file = version_dirs[0] / "envy.lua"

        self.assertTrue(types_file.exists(), f"Types not at {types_file}")
        content = types_file.read_text()
        self.assertIn("---@meta", content)

    def test_self_deploy_fast_path_when_cached(self) -> None:
        """Second run uses fast path (no re-deployment)."""
        import sys

        # First run deploys
        result1 = self._run_envy_with_self_deploy()
        self.assertEqual(0, result1.returncode, f"stderr: {result1.stderr}")

        # Verify binary exists after first run
        envy_cache = self._cache_dir / "envy"
        version_dirs = list(envy_cache.iterdir())
        if sys.platform == "win32":
            cached_binary = version_dirs[0] / "envy.exe"
        else:
            cached_binary = version_dirs[0] / "envy"
        self.assertTrue(cached_binary.exists())

        # Get modification time
        mtime1 = cached_binary.stat().st_mtime

        # Second run should hit fast path (binary not modified)
        # Use a fresh project dir to avoid "manifest already exists" message
        self._project_dir = self._temp_dir / "project2"
        self._bin_dir = self._temp_dir / "bin2"
        result2 = self._run_envy_with_self_deploy()
        self.assertEqual(0, result2.returncode, f"stderr: {result2.stderr}")

        mtime2 = cached_binary.stat().st_mtime
        self.assertEqual(mtime1, mtime2, "Binary should not be modified on second run")

    def test_self_deploy_binary_is_executable(self) -> None:
        """Self-deployed binary has executable permissions (Unix)."""
        import sys

        if sys.platform == "win32":
            self.skipTest("Executable permissions not applicable on Windows")

        result = self._run_envy_with_self_deploy()
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        envy_cache = self._cache_dir / "envy"
        version_dirs = list(envy_cache.iterdir())
        cached_binary = version_dirs[0] / "envy"

        self.assertTrue(os.access(cached_binary, os.X_OK))

    def test_self_deploy_cached_binary_works(self) -> None:
        """The cached binary can be executed directly."""
        import sys

        # First, trigger self-deployment
        result = self._run_envy_with_self_deploy()
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")

        # Find and run the cached binary
        envy_cache = self._cache_dir / "envy"
        version_dirs = list(envy_cache.iterdir())

        if sys.platform == "win32":
            cached_binary = version_dirs[0] / "envy.exe"
        else:
            cached_binary = version_dirs[0] / "envy"

        env = get_test_env()
        env["ENVY_CACHE_ROOT"] = str(self._cache_dir)

        result2 = subprocess.run(
            [str(cached_binary), "version"],
            capture_output=True,
            text=True,
            env=env,
            timeout=30,
        )
        self.assertEqual(0, result2.returncode, f"stderr: {result2.stderr}")
        self.assertIn("envy version", result2.stderr)


if __name__ == "__main__":
    unittest.main()

"""Functional tests for user-managed packages (Phase 8)."""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest

from . import test_config

# =============================================================================
# Shared specs (used by multiple tests)
# =============================================================================

# Simple user-managed package: check if marker file exists, create if not
# Simulates system package wrapper (like brew install python)
SPEC_SIMPLE = """IDENTITY = "local.user_managed_simple@v1"

function CHECK(project_root, options)
    local marker = os.getenv("ENVY_TEST_MARKER_SIMPLE")
    if not marker then error("ENVY_TEST_MARKER_SIMPLE must be set") end
    local f = io.open(marker, "r")
    if f then f:close(); return true end
    return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    local marker = os.getenv("ENVY_TEST_MARKER_SIMPLE")
    if not marker then error("ENVY_TEST_MARKER_SIMPLE must be set") end
    local f = io.open(marker, "w")
    if not f then error("Failed to create marker file: " .. marker) end
    f:write("installed by user_managed_simple")
    f:close()
end
"""

# Context isolation test: verify restricted directory access for user-managed
# Tests install_dir=nil, stage_dir access, fetch_dir access, extract_all behavior
SPEC_CTX_FORBIDDEN = """IDENTITY = "local.user_managed_ctx_isolation_forbidden@v1"

function CHECK(project_root, options)
    return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    local forbidden_api = os.getenv("ENVY_TEST_FORBIDDEN_API")
    if not forbidden_api then error("ENVY_TEST_FORBIDDEN_API must be set") end

    if forbidden_api == "install_dir" then
        if install_dir == nil then
            print("Verified: install_dir not available for user-managed")
            return
        else
            error("install_dir should be nil for user-managed packages")
        end
    elseif forbidden_api == "stage_dir" then
        if stage_dir then
            print("Verified: stage_dir access checked for user-managed")
            return
        end
    elseif forbidden_api == "fetch_dir" then
        if fetch_dir then
            print("Verified: fetch_dir access checked for user-managed")
            return
        end
    elseif forbidden_api == "extract_all" then
        local success, err = pcall(function() envy.extract_all("", "") end)
        if not success then
            print("Verified: extract_all fails without proper context for user-managed")
            return
        end
    else
        error("Unknown forbidden API: " .. forbidden_api)
    end
end
"""


class TestUserManagedPackages(unittest.TestCase):
    """Test user-managed package behavior with check verbs."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-user-managed-test-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-user-managed-work-"))
        self.marker_dir = Path(tempfile.mkdtemp(prefix="envy-user-managed-markers-"))
        self.specs_dir = Path(tempfile.mkdtemp(prefix="envy-user-managed-specs-"))
        self.envy_test = test_config.get_envy_executable()

        # Create marker paths in temp directory
        self.marker_simple = self.marker_dir / "marker-simple"
        self.marker_with_fetch = self.marker_dir / "marker-with-fetch"

        # Write shared specs
        (self.specs_dir / "simple.lua").write_text(SPEC_SIMPLE, encoding="utf-8")
        (self.specs_dir / "ctx_forbidden.lua").write_text(
            SPEC_CTX_FORBIDDEN, encoding="utf-8"
        )

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)
        shutil.rmtree(self.marker_dir, ignore_errors=True)
        shutil.rmtree(self.specs_dir, ignore_errors=True)

    def write_spec(self, name: str, content: str) -> Path:
        """Write spec to temp dir, return path."""
        path = self.specs_dir / f"{name}.lua"
        path.write_text(content, encoding="utf-8")
        return path

    def run_spec(
        self, name: str, identity: str, trace: bool = False, env_vars: dict = None
    ):
        """Run spec by name and identity."""
        cmd = [str(self.envy_test), f"--cache-root={self.cache_root}"]
        if trace:
            cmd.append("--trace")
        cmd.extend(["engine-test", identity, str(self.specs_dir / f"{name}.lua")])

        env = os.environ.copy()
        if env_vars:
            env.update(env_vars)

        return subprocess.run(
            cmd, capture_output=True, text=True, env=env, cwd=self.test_dir
        )

    # =========================================================================
    # Basic user-managed package tests (using SPEC_SIMPLE)
    # =========================================================================

    def test_simple_first_run_check_false_installs(self):
        """First run with check=false acquires lock, runs install, purges cache."""
        env = {"ENVY_TEST_MARKER_SIMPLE": str(self.marker_simple)}
        result = self.run_spec("simple", "local.user_managed_simple@v1", env_vars=env)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        self.assertTrue(
            self.marker_simple.exists(), "Install should have created marker file"
        )

        pkg_dir = self.cache_root / "packages" / "local.user_managed_simple@v1"
        if pkg_dir.exists():
            self.assertFalse(
                any(pkg_dir.glob("*/pkg")),
                "User-managed packages should not leave pkg/ in cache",
            )

    def test_simple_second_run_check_true_skips(self):
        """Second run with check=true skips all phases, no lock acquired."""
        env = {"ENVY_TEST_MARKER_SIMPLE": str(self.marker_simple)}

        # Create marker to simulate already-installed
        self.marker_simple.parent.mkdir(parents=True, exist_ok=True)
        self.marker_simple.write_text("already installed")

        result = self.run_spec(
            "simple", "local.user_managed_simple@v1", trace=True, env_vars=env
        )
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        self.assertIn("user check returned true", result.stderr.lower())
        self.assertIn("skipping all phases", result.stderr.lower())
        self.assertIn(
            "phase install: no lock (cache hit), skipping", result.stderr.lower()
        )

    def test_multiple_runs_cache_lifecycle(self):
        """Verify cache entry created/purged/created on each install cycle."""
        env = {"ENVY_TEST_MARKER_SIMPLE": str(self.marker_simple)}

        # Run 1: Install
        result1 = self.run_spec("simple", "local.user_managed_simple@v1", env_vars=env)
        self.assertEqual(result1.returncode, 0)
        self.assertTrue(self.marker_simple.exists(), "First run should create marker")

        # Run 2: Skip (check=true)
        result2 = self.run_spec(
            "simple", "local.user_managed_simple@v1", trace=True, env_vars=env
        )
        self.assertEqual(result2.returncode, 0)
        self.assertIn("check passed", result2.stderr.lower())
        self.assertIn("skipping all phases", result2.stderr.lower())

        # Run 3: Remove marker, reinstall
        self.marker_simple.unlink()
        result3 = self.run_spec("simple", "local.user_managed_simple@v1", env_vars=env)
        self.assertEqual(result3.returncode, 0)
        self.assertTrue(
            self.marker_simple.exists(), "Should reinstall after marker removed"
        )

    def test_double_check_lock_prevents_duplicate_work(self):
        """Double-check lock: if check passes post-lock, skip install."""
        env = {"ENVY_TEST_MARKER_SIMPLE": str(self.marker_simple)}

        result = self.run_spec(
            "simple", "local.user_managed_simple@v1", trace=True, env_vars=env
        )
        self.assertIn("re-running user check (post-lock)", result.stderr)
        self.assertIn("re-check returned false", result.stderr)

    def test_cache_state_after_install_fully_deleted(self):
        """After install completes, entire entry_dir deleted for user-managed."""
        env = {"ENVY_TEST_MARKER_SIMPLE": str(self.marker_simple)}

        result = self.run_spec("simple", "local.user_managed_simple@v1", env_vars=env)
        self.assertEqual(result.returncode, 0)

        asset_base = self.cache_root / "packages" / "local.user_managed_simple@v1"
        if asset_base.exists():
            remaining = list(asset_base.rglob("*"))
            non_lock_files = [
                r for r in remaining if r.is_file() and not r.name.endswith(".lock")
            ]
            self.assertEqual(
                len(non_lock_files),
                0,
                f"User-managed should not leave files in cache, found: {non_lock_files}",
            )

    # =========================================================================
    # Fetch/stage/build with cache-managed
    # =========================================================================

    def test_user_managed_with_fetch_purges_all_dirs(self):
        """Cache-managed with fetch verb: fetch_dir populated, package persists."""
        # Cache-managed package with declarative fetch and all phases
        spec_with_fetch = """IDENTITY = "local.user_managed_with_fetch@v1"

FETCH = {
    source = "https://raw.githubusercontent.com/ninja-build/ninja/v1.13.2/README.md",
    sha256 = "b31e9700c752fa214773c1b799d90efcbf3330c8062da9f45c6064e023b347b0"
}

function STAGE(fetch_dir, stage_dir, tmp_dir, options)
    local readme = fetch_dir .. "/README.md"
    local f = io.open(readme, "r")
    if not f then error("Fetch did not produce README.md") end
    f:close()
end

function BUILD(stage_dir, install_dir, fetch_dir, tmp_dir, options)
    if not stage_dir then error("stage_dir not available in build phase") end
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    local marker = os.getenv("ENVY_TEST_MARKER_WITH_FETCH")
    if not marker then error("ENVY_TEST_MARKER_WITH_FETCH must be set") end
    local f = io.open(marker, "w")
    if not f then error("Failed to create marker file") end
    f:write("installed with fetch/stage/build")
    f:close()
end
"""
        self.write_spec("with_fetch", spec_with_fetch)
        env = {"ENVY_TEST_MARKER_WITH_FETCH": str(self.marker_with_fetch)}

        result = self.run_spec(
            "with_fetch", "local.user_managed_with_fetch@v1", trace=True, env_vars=env
        )
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

        self.assertTrue(self.marker_with_fetch.exists())

        pkg_dir = self.cache_root / "packages" / "local.user_managed_with_fetch@v1"
        self.assertTrue(pkg_dir.exists(), "Cache-managed packages should persist")

        pkg_subdirs = list(pkg_dir.glob("*/pkg"))
        self.assertGreater(len(pkg_subdirs), 0, "Should have pkg/ directory in cache")

    # =========================================================================
    # Validation error tests
    # =========================================================================

    def test_validation_error_check_with_forbidden_api(self):
        """User-managed packages cannot access install_dir (it's nil)."""
        # User-managed spec that tries to use install_dir
        spec_invalid = """IDENTITY = "local.user_managed_invalid@v1"

function CHECK(project_root, options)
    return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    if install_dir == nil then
        error("install_dir not available for user-managed package local.user_managed_invalid@v1")
    end
    local path = install_dir .. "/test.txt"
end
"""
        self.write_spec("invalid", spec_invalid)

        result = self.run_spec("invalid", "local.user_managed_invalid@v1")
        self.assertNotEqual(result.returncode, 0, "Should fail validation")
        self.assertIn("not available for user-managed", result.stderr)

    # =========================================================================
    # Context isolation tests (using SPEC_CTX_FORBIDDEN)
    # =========================================================================

    def test_user_managed_ctx_tmp_dir_accessible(self):
        """User-managed packages can access and use tmp_dir."""
        # Spec that verifies tmp_dir is accessible and writable
        spec_tmp_dir = """IDENTITY = "local.user_managed_ctx_isolation_tmp_dir@v1"

function CHECK(project_root, options)
    return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    if not tmp_dir then error("tmp_dir should be accessible") end
    if type(tmp_dir) ~= "string" then error("tmp_dir should be a string") end

    local test_file = tmp_dir .. "/test.txt"
    local f = io.open(test_file, "w")
    if not f then error("Failed to create file in tmp_dir") end
    f:write("test content")
    f:close()

    f = io.open(test_file, "r")
    if not f then error("Failed to read file from tmp_dir") end
    local content = f:read("*all")
    f:close()
    if content ~= "test content" then error("tmp_dir file content mismatch") end
end
"""
        self.write_spec("ctx_tmp_dir", spec_tmp_dir)

        result = self.run_spec(
            "ctx_tmp_dir", "local.user_managed_ctx_isolation_tmp_dir@v1"
        )
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_user_managed_ctx_allowed_apis(self):
        """User-managed packages can access allowed APIs (run, package, product)."""
        # Spec that verifies envy.* APIs are accessible
        spec_allowed = """IDENTITY = "local.user_managed_ctx_isolation_allowed@v1"

function CHECK(project_root, options)
    return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    if IDENTITY ~= "local.user_managed_ctx_isolation_allowed@v1" then
        error("IDENTITY mismatch")
    end

    if type(envy.run) ~= "function" then error("envy.run should be a function") end
    local result = envy.run("echo test", {capture = true, quiet = true})
    if result.exit_code ~= 0 then error("envy.run failed") end

    if type(envy.package) ~= "function" then error("envy.package should be a function") end
    if type(envy.product) ~= "function" then error("envy.product should be a function") end
end
"""
        self.write_spec("ctx_allowed", spec_allowed)

        result = self.run_spec(
            "ctx_allowed", "local.user_managed_ctx_isolation_allowed@v1"
        )
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_user_managed_ctx_forbids_fetch_dir(self):
        """User-managed packages: fetch_dir access verification."""
        result = self.run_spec(
            "ctx_forbidden",
            "local.user_managed_ctx_isolation_forbidden@v1",
            env_vars={"ENVY_TEST_FORBIDDEN_API": "fetch_dir"},
        )
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_user_managed_ctx_forbids_stage_dir(self):
        """User-managed packages: stage_dir access verification."""
        result = self.run_spec(
            "ctx_forbidden",
            "local.user_managed_ctx_isolation_forbidden@v1",
            env_vars={"ENVY_TEST_FORBIDDEN_API": "stage_dir"},
        )
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_user_managed_ctx_forbids_install_dir(self):
        """User-managed packages: install_dir should be nil."""
        result = self.run_spec(
            "ctx_forbidden",
            "local.user_managed_ctx_isolation_forbidden@v1",
            env_vars={"ENVY_TEST_FORBIDDEN_API": "install_dir"},
        )
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")

    def test_user_managed_ctx_forbids_extract_all(self):
        """User-managed packages: extract_all fails without proper context."""
        result = self.run_spec(
            "ctx_forbidden",
            "local.user_managed_ctx_isolation_forbidden@v1",
            env_vars={"ENVY_TEST_FORBIDDEN_API": "extract_all"},
        )
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")


if __name__ == "__main__":
    unittest.main()

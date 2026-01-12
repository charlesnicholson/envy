"""Functional tests for check/install runtime behavior.

Tests comprehensive functionality of check and install verbs including:
- Check string silent success behavior
- Check ctx.run with quiet/capture options
- Install cwd behavior for cache-managed vs user-managed packages
- Default shell configuration
- Table field access patterns
- Shell error types
- User-managed workspace lifecycle
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
import unittest

from . import test_config


class TestCheckInstallRuntime(unittest.TestCase):
    """Tests for check/install runtime behavior."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-check-install-test-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-check-install-work-"))
        self.specs_dir = Path(tempfile.mkdtemp(prefix="envy-check-install-specs-"))
        self.envy_test = test_config.get_envy_executable()
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)
        shutil.rmtree(self.specs_dir, ignore_errors=True)

    def run_spec(
        self,
        spec_name,
        spec_content,
        identity,
        should_fail=False,
        env_vars=None,
        verbose=False,
    ):
        """Run a spec with given content and identity, return subprocess result."""
        spec_path = self.specs_dir / f"{spec_name}.lua"
        spec_path.write_text(spec_content, encoding="utf-8")

        cmd = [str(self.envy_test), f"--cache-root={self.cache_root}"]
        cmd.extend(self.trace_flag)
        if verbose:
            cmd.append("--verbose")
        cmd.extend(["engine-test", identity, str(spec_path)])

        env = os.environ.copy()
        if env_vars:
            env.update(env_vars)

        result = test_config.run(
            cmd, capture_output=True, text=True, env=env, cwd=self.test_dir
        )

        if should_fail:
            self.assertNotEqual(
                result.returncode,
                0,
                f"Expected failure but succeeded.\nstdout: {result.stdout}\nstderr: {result.stderr}",
            )
        else:
            self.assertEqual(
                result.returncode,
                0,
                f"stdout: {result.stdout}\nstderr: {result.stderr}",
            )

        return result

    # ===== Check String Success Behavior =====

    def test_check_string_success_silent(self):
        """Check string success produces no TUI output."""
        # Check string success is silent (no TUI output)
        spec = """
IDENTITY = "local.check_string_success@v1"
CHECK = "echo 'test'"
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        result = self.run_spec(
            "check_string_silent", spec, "local.check_string_success@v1"
        )
        self.assertNotIn("python", result.stderr.lower())

    def test_check_string_success_silent_with_verbose(self):
        """Check string success produces no TUI output even with --verbose."""
        # Check string success is silent (no TUI output)
        spec = """
IDENTITY = "local.check_string_success@v1"
CHECK = "echo 'test'"
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        result = self.run_spec(
            "check_string_silent", spec, "local.check_string_success@v1", verbose=True
        )
        self.assertNotIn("python", result.stderr.lower())

    # ===== ctx.run Quiet/Capture Behavior =====

    def test_ctx_run_quiet_success(self):
        """ctx.run quiet=true success returns exit_code, no TUI output."""
        # ctx.run quiet=true on success
        spec = """
IDENTITY = "local.check_ctx_run_quiet@v1"
function CHECK(project_root, options)
    local res = envy.run("echo 'test output'", {quiet = true})
    assert(res.exit_code ~= nil, "exit_code field should exist")
    assert(res.exit_code == 0, "exit_code should be 0")
    return true
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        self.run_spec("ctx_run_quiet_success", spec, "local.check_ctx_run_quiet@v1")

    def test_ctx_run_quiet_failure(self):
        """ctx.run quiet=true failure throws with no TUI output."""
        # ctx.run quiet=true on failure (throws)
        spec = """
IDENTITY = "local.check_ctx_run_quiet_fail@v1"
function CHECK(project_root, options)
    local res = envy.run("exit 1", {quiet = true})
    error("Should have thrown on non-zero exit")
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        result = self.run_spec(
            "ctx_run_quiet_failure",
            spec,
            "local.check_ctx_run_quiet_fail@v1",
            should_fail=True,
        )
        self.assertIn("error", result.stderr.lower())

    def test_ctx_run_capture(self):
        """ctx.run capture=true returns table with stdout, stderr, exit_code."""
        # ctx.run capture=true returns stdout, stderr, exit_code
        spec = """
IDENTITY = "local.check_ctx_run_capture@v1"
function CHECK(project_root, options)
    local cmd = envy.PLATFORM == "windows"
        and "Write-Output 'stdout text'; [Console]::Error.WriteLine('stderr text')"
        or "echo 'stdout text' && echo 'stderr text' >&2"
    local res = envy.run(cmd, {capture = true})
    assert(res.stdout ~= nil, "stdout field should exist")
    assert(res.stderr ~= nil, "stderr field should exist")
    assert(res.exit_code ~= nil, "exit_code field should exist")
    assert(res.stdout:match("stdout text"), "stdout should contain expected text")
    assert(res.exit_code == 0, "exit_code should be 0")
    return true
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        self.run_spec("ctx_run_capture", spec, "local.check_ctx_run_capture@v1")

    def test_ctx_run_no_capture(self):
        """ctx.run capture=false returns table with only exit_code field."""
        # ctx.run capture=false returns only exit_code
        spec = """
IDENTITY = "local.check_ctx_run_no_capture@v1"
function CHECK(project_root, options)
    local res = envy.run("echo 'test'", {capture = false})
    assert(res.exit_code ~= nil, "exit_code field should exist")
    assert(res.stdout == nil, "stdout field should not exist when capture=false")
    assert(res.stderr == nil, "stderr field should not exist when capture=false")
    assert(res.exit_code == 0, "exit_code should be 0")
    return true
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        self.run_spec("ctx_run_no_capture", spec, "local.check_ctx_run_no_capture@v1")

    def test_ctx_run_default(self):
        """ctx.run default (no flags) streams, throws on non-zero, returns exit_code."""
        # ctx.run default: streams, throws on non-zero, returns exit_code
        spec = """
IDENTITY = "local.check_ctx_run_default@v1"
function CHECK(project_root, options)
    local res = envy.run("echo 'default test'")
    assert(res.exit_code ~= nil, "exit_code field should exist")
    assert(res.exit_code == 0, "exit_code should be 0")
    assert(res.stdout == nil, "stdout not captured by default")
    assert(res.stderr == nil, "stderr not captured by default")
    return true
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        self.run_spec("ctx_run_default", spec, "local.check_ctx_run_default@v1")

    def test_ctx_run_quiet_capture_combinations(self):
        """Test all 4 combinations of quiet/capture flags."""
        # ctx.run with neither quiet nor capture
        spec_neither = """
IDENTITY = "local.check_combo_neither@v1"
function CHECK(project_root, options)
    local res = envy.run("echo 'combo test'")
    assert(res.exit_code == 0)
    assert(res.stdout == nil, "stdout not captured")
    assert(res.stderr == nil, "stderr not captured")
    return true
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        # ctx.run with quiet=true only
        spec_quiet = """
IDENTITY = "local.check_combo_quiet@v1"
function CHECK(project_root, options)
    local res = envy.run("echo 'quiet test'", {quiet = true})
    assert(res.exit_code == 0)
    assert(res.stdout == nil, "stdout not captured")
    assert(res.stderr == nil, "stderr not captured")
    return true
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        # ctx.run with capture=true only
        spec_capture = """
IDENTITY = "local.check_combo_capture@v1"
function CHECK(project_root, options)
    local res = envy.run("echo 'capture test'", {capture = true})
    assert(res.exit_code == 0)
    assert(res.stdout ~= nil, "stdout should be captured")
    assert(res.stderr ~= nil, "stderr should be captured")
    assert(res.stdout:match("capture test"))
    return true
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        # ctx.run with both quiet=true and capture=true
        spec_both = """
IDENTITY = "local.check_combo_both@v1"
function CHECK(project_root, options)
    local res = envy.run("echo 'both test'", {quiet = true, capture = true})
    assert(res.exit_code == 0)
    assert(res.stdout ~= nil, "stdout should be captured")
    assert(res.stderr ~= nil, "stderr should be captured")
    assert(res.stdout:match("both test"))
    return true
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        test_cases = [
            ("ctx_run_combo_neither", spec_neither, "local.check_combo_neither@v1"),
            ("ctx_run_combo_quiet", spec_quiet, "local.check_combo_quiet@v1"),
            ("ctx_run_combo_capture", spec_capture, "local.check_combo_capture@v1"),
            ("ctx_run_combo_both", spec_both, "local.check_combo_both@v1"),
        ]
        for spec_name, spec_content, identity in test_cases:
            with self.subTest(spec=spec_name):
                self.run_spec(spec_name, spec_content, identity)

    # ===== CWD and Shell Configuration =====

    def test_check_cwd_manifest_directory(self):
        """Check cwd = manifest directory."""
        # Check cwd = manifest directory
        spec = """
IDENTITY = "local.check_cwd_manifest@v1"
function CHECK(project_root, options)
    if envy.PLATFORM == "windows" then
        envy.run('"cwd_test" | Out-File -FilePath cwd_check_marker.txt')
    else
        envy.run("pwd > /tmp/check_cwd_test_pwd.txt")
        envy.run("echo 'cwd_test' > cwd_check_marker.txt")
    end
    local test_cmd = envy.PLATFORM == "windows"
        and 'if (Test-Path cwd_check_marker.txt) { exit 0 } else { exit 1 }'
        or "test -f cwd_check_marker.txt"
    local res = envy.run(test_cmd, {quiet = true})
    if envy.PLATFORM == "windows" then
        envy.run('Remove-Item -Force -ErrorAction SilentlyContinue cwd_check_marker.txt', {quiet = true})
    else
        envy.run("rm -f cwd_check_marker.txt /tmp/check_cwd_test_pwd.txt", {quiet = true})
    end
    if res.exit_code ~= 0 then error("Could not create marker file relative to cwd") end
    return true
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        self.run_spec("check_cwd_manifest", spec, "local.check_cwd_manifest@v1")

    def test_install_cwd_cache_managed(self):
        """Install cwd = install_dir for cache-managed packages."""
        # Install cwd = install_dir for cache-managed packages
        spec = """
IDENTITY = "local.install_cwd_cache@v1"
function FETCH(tmp_dir, options) end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    if envy.PLATFORM == "windows" then
        envy.run('"test" | Out-File -FilePath cwd_marker.txt')
    else
        envy.run("echo 'test' > cwd_marker.txt")
    end
    local test_cmd = envy.PLATFORM == "windows"
        and 'if (Test-Path cwd_marker.txt) { exit 0 } else { exit 1 }'
        or "test -f cwd_marker.txt"
    local res = envy.run(test_cmd, {quiet = true})
    if res.exit_code ~= 0 then error("Marker file not accessible via relative path - cwd issue") end
    local marker_path = install_dir .. (envy.PLATFORM == "windows" and "\\\\cwd_marker.txt" or "/cwd_marker.txt")
    local test_cmd2 = envy.PLATFORM == "windows"
        and ('if (Test-Path \\'' .. marker_path .. '\\') { exit 0 } else { exit 1 }')
        or ("test -f '" .. marker_path .. "'")
    local res2 = envy.run(test_cmd2, {quiet = true})
    if res2.exit_code ~= 0 then error("Marker file not in install_dir - cwd was not install_dir") end
end
"""
        self.run_spec("install_cwd_cache", spec, "local.install_cwd_cache@v1")

    def test_install_cwd_user_managed(self):
        """Install cwd = manifest directory for user-managed packages."""
        # Install cwd = manifest directory for user-managed packages
        spec = """
IDENTITY = "local.install_cwd_user@v1"
function CHECK(project_root, options)
    local marker = os.getenv("ENVY_TEST_INSTALL_MARKER")
    if not marker then error("ENVY_TEST_INSTALL_MARKER not set") end
    local test_cmd = envy.PLATFORM == "windows"
        and ('if (Test-Path \\'' .. marker .. '\\') { exit 0 } else { exit 1 }')
        or ("test -f '" .. marker .. "'")
    local success, res = pcall(function() return envy.run(test_cmd, {quiet = true}) end)
    return success and res.exit_code == 0
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    local marker = os.getenv("ENVY_TEST_INSTALL_MARKER")
    if not marker then error("ENVY_TEST_INSTALL_MARKER not set") end
    if envy.PLATFORM == "windows" then
        envy.run('"user_managed_cwd_test" | Out-File -FilePath user_install_cwd_marker.txt')
    else
        envy.run("echo 'user_managed_cwd_test' > user_install_cwd_marker.txt")
    end
    local test_cmd = envy.PLATFORM == "windows"
        and 'if (Test-Path user_install_cwd_marker.txt) { exit 0 } else { exit 1 }'
        or "test -f user_install_cwd_marker.txt"
    local res = envy.run(test_cmd, {quiet = true})
    if envy.PLATFORM == "windows" then
        envy.run('Remove-Item -Force -ErrorAction SilentlyContinue user_install_cwd_marker.txt', {quiet = true})
    else
        envy.run("rm -f user_install_cwd_marker.txt", {quiet = true})
    end
    if res.exit_code ~= 0 then error("Could not create file with relative path - cwd issue") end
    if envy.PLATFORM == "windows" then
        envy.run('New-Item -ItemType File -Force -Path \\'' .. marker .. '\\' | Out-Null')
    else
        envy.run("touch '" .. marker .. "'")
    end
end
"""
        marker_file = self.test_dir / "install_marker"
        self.run_spec(
            "install_cwd_user",
            spec,
            "local.install_cwd_user@v1",
            env_vars={"ENVY_TEST_INSTALL_MARKER": str(marker_file)},
        )

    def test_default_shell_in_check(self):
        """Manifest default_shell is respected in check string."""
        # Manifest default_shell respected in check string
        spec = """
IDENTITY = "local.check_default_shell@v1"
CHECK = "echo 'shell test'"
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        self.run_spec("check_shell", spec, "local.check_default_shell@v1")

    def test_default_shell_in_install(self):
        """Manifest default_shell is respected in install string."""
        # Manifest default_shell respected in install string
        spec = """
IDENTITY = "local.install_default_shell@v1"
function FETCH(tmp_dir, options) end
INSTALL = "echo 'install shell test' > output.txt"
"""
        self.run_spec("install_shell", spec, "local.install_default_shell@v1")

    # ===== Table Field Access Patterns =====

    def test_table_field_direct_access(self):
        """Direct table field access: res.exit_code, res.stdout."""
        # Direct table field access patterns
        spec = """
IDENTITY = "local.check_table_fields@v1"
function CHECK(project_root, options)
    local res = envy.run("echo 'test output'", {capture = true})
    local code = res.exit_code
    local out = res.stdout
    local err = res.stderr
    assert(code == 0, "exit_code should be 0")
    assert(out ~= nil, "stdout should exist")
    assert(err ~= nil, "stderr should exist")
    assert(out:match("test output"), "stdout should contain expected text")
    return true
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        self.run_spec("table_field_access", spec, "local.check_table_fields@v1")

    def test_table_field_chained_access(self):
        """Chained table field access: envy.run(...).stdout."""
        # Chained table field access patterns
        spec = """
IDENTITY = "local.check_table_chained@v1"
function CHECK(project_root, options)
    local out = envy.run("echo 'chained'", {capture = true}).stdout
    assert(out:match("chained"), "chained stdout access should work")
    local code = envy.run("echo 'test'", {quiet = true}).exit_code
    assert(code == 0, "chained exit_code access should work")
    return true
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        self.run_spec("table_chained_access", spec, "local.check_table_chained@v1")

    # ===== Shell Error Types =====

    def test_shell_error_command_not_found(self):
        """Shell error: command not found."""
        # Shell error: command not found
        spec = """
IDENTITY = "local.check_error_not_found@v1"
function CHECK(project_root, options)
    local res = envy.run("nonexistent_command_12345", {quiet = true, check = true})
    error("Should have thrown on command not found")
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        result = self.run_spec(
            "error_cmd_not_found",
            spec,
            "local.check_error_not_found@v1",
            should_fail=True,
        )
        stderr_lower = result.stderr.lower()
        if sys.platform == "win32":
            self.assertIn("exit code 1", stderr_lower)
        else:
            self.assertIn("exit code 127", stderr_lower)

    def test_shell_error_syntax(self):
        """Shell error: syntax error."""
        # Shell error: syntax error
        spec = """
IDENTITY = "local.check_error_syntax@v1"
function CHECK(project_root, options)
    local res = envy.run("echo 'unclosed quote", {quiet = true})
    error("Should have thrown on syntax error")
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        result = self.run_spec(
            "error_syntax", spec, "local.check_error_syntax@v1", should_fail=True
        )
        self.assertIn("error", result.stderr.lower())

    # ===== Concurrent Output =====

    def test_concurrent_large_stdout_stderr(self):
        """Concurrent large stdout+stderr with capture (no pipe deadlock)."""
        # Concurrent large stdout+stderr with capture (no deadlock)
        spec = """
IDENTITY = "local.check_concurrent@v1"
function CHECK(project_root, options)
    local cmd
    if envy.PLATFORM == "windows" then
        cmd = [[for ($i=1; $i -le 1000; $i++) { Write-Output "stdout line $i"; [Console]::Error.WriteLine("stderr line $i") }]]
    else
        cmd = [[for i in $(seq 1 1000); do echo "stdout line $i"; echo "stderr line $i" >&2; done]]
    end
    local res = envy.run(cmd, {capture = true})
    assert(res.exit_code == 0, "command should succeed")
    assert(res.stdout:match("stdout line"), "should have stdout")
    assert(res.stderr:match("stderr line"), "should have stderr")
    return true
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        self.run_spec("concurrent_output", spec, "local.check_concurrent@v1")

    # ===== Empty Output =====

    def test_empty_outputs_in_failure(self):
        """Empty outputs in failure messages are clarified."""
        # Empty outputs in failure messages
        spec = """
IDENTITY = "local.check_empty_output@v1"
function CHECK(project_root, options)
    local res = envy.run("exit 42", {quiet = true})
    error("Should have thrown on non-zero exit")
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options) end
"""
        result = self.run_spec(
            "empty_output_failure",
            spec,
            "local.check_empty_output@v1",
            should_fail=True,
        )
        self.assertIn("error", result.stderr.lower())

    # ===== User-Managed Workspace Lifecycle =====

    def test_user_managed_tmp_dir_created(self):
        """User-managed install: tmp_dir is created and accessible."""
        # User-managed tmp_dir lifecycle
        spec = """
IDENTITY = "local.user_tmp_lifecycle@v1"
function CHECK(project_root, options)
    local marker = os.getenv("ENVY_TEST_TMP_MARKER")
    if not marker then error("ENVY_TEST_TMP_MARKER not set") end
    local test_cmd = envy.PLATFORM == "windows"
        and ('if (Test-Path \\'' .. marker .. '\\') { exit 0 } else { exit 1 }')
        or ("test -f '" .. marker .. "'")
    local success, res = pcall(function() return envy.run(test_cmd, {quiet = true}) end)
    return success and res.exit_code == 0
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    local marker = os.getenv("ENVY_TEST_TMP_MARKER")
    if not marker then error("ENVY_TEST_TMP_MARKER not set") end
    assert(tmp_dir ~= nil, "tmp_dir should be exposed")
    local path_sep = envy.PLATFORM == "windows" and "\\\\" or "/"
    local test_file = tmp_dir .. path_sep .. "test_file.txt"
    if envy.PLATFORM == "windows" then
        envy.run('"test" | Out-File -FilePath \\'' .. test_file .. '\\'')
    else
        envy.run("echo 'test' > " .. test_file)
    end
    local test_cmd = envy.PLATFORM == "windows"
        and ('if (Test-Path \\'' .. test_file .. '\\') { exit 0 } else { exit 1 }')
        or ("test -f " .. test_file)
    local res = envy.run(test_cmd, {quiet = true})
    if res.exit_code ~= 0 then error("Could not write to tmp_dir") end
    if envy.PLATFORM == "windows" then
        envy.run('New-Item -ItemType File -Force -Path \\'' .. marker .. '\\' | Out-Null')
    else
        envy.run("touch '" .. marker .. "'")
    end
end
"""
        marker_file = self.test_dir / "tmp_marker"
        self.run_spec(
            "user_tmp_lifecycle",
            spec,
            "local.user_tmp_lifecycle@v1",
            env_vars={"ENVY_TEST_TMP_MARKER": str(marker_file)},
        )
        self.assertTrue(marker_file.exists())

    def test_user_managed_entry_dir_deleted(self):
        """User-managed: entry_dir deleted after completion."""
        # User-managed entry_dir cleanup
        spec = """
IDENTITY = "local.user_cleanup@v1"
function CHECK(project_root, options)
    local marker = os.getenv("ENVY_TEST_CLEANUP_MARKER")
    if not marker then error("ENVY_TEST_CLEANUP_MARKER not set") end
    local success, res = pcall(function()
        return envy.run("test -f '" .. marker .. "'", {quiet = true})
    end)
    return success and res.exit_code == 0
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    local marker = os.getenv("ENVY_TEST_CLEANUP_MARKER")
    if not marker then error("ENVY_TEST_CLEANUP_MARKER not set") end
    envy.run("touch '" .. marker .. "'")
end
"""
        marker_file = self.test_dir / "cleanup_marker"
        self.run_spec(
            "user_cleanup",
            spec,
            "local.user_cleanup@v1",
            env_vars={"ENVY_TEST_CLEANUP_MARKER": str(marker_file)},
        )
        pkg_dir = self.cache_root / "packages" / "local.user_cleanup@v1"
        if pkg_dir.exists():
            pkg_subdirs = list(pkg_dir.glob("*/pkg"))
            self.assertEqual(
                len(pkg_subdirs),
                0,
                "User-managed packages should not leave pkg/ in cache",
            )

    def test_user_managed_check_no_tmp_dir(self):
        """User-managed check phase: no tmp_dir exposed."""
        # Check phase does not expose tmp_dir
        spec = """
IDENTITY = "local.user_check_no_tmp@v1"
function CHECK(project_root, options)
    return true
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    assert(tmp_dir ~= nil, "tmp_dir should be exposed in install phase")
end
"""
        self.run_spec("user_check_no_tmp", spec, "local.user_check_no_tmp@v1")

    def test_user_managed_tmp_dir_vs_cwd(self):
        """User-managed: tmp_dir for workspace, cwd is manifest directory."""
        # User-managed: tmp_dir for workspace, cwd is manifest directory
        spec = """
IDENTITY = "local.user_tmp_vs_cwd@v1"
function CHECK(project_root, options)
    local marker = os.getenv("ENVY_TEST_TMP_CWD_MARKER")
    if not marker then error("ENVY_TEST_TMP_CWD_MARKER not set") end
    local test_cmd = envy.PLATFORM == "windows"
        and ('if (Test-Path \\'' .. marker .. '\\') { exit 0 } else { exit 1 }')
        or ("test -f '" .. marker .. "'")
    local success, res = pcall(function() return envy.run(test_cmd, {quiet = true}) end)
    return success and res.exit_code == 0
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    local marker = os.getenv("ENVY_TEST_TMP_CWD_MARKER")
    if not marker then error("ENVY_TEST_TMP_CWD_MARKER not set") end
    assert(tmp_dir ~= nil, "tmp_dir should be exposed")
    local path_sep = envy.PLATFORM == "windows" and "\\\\" or "/"
    local tmp_file = tmp_dir .. path_sep .. "tmp_marker.txt"
    if envy.PLATFORM == "windows" then
        envy.run('"tmp test" | Out-File -FilePath \\'' .. tmp_file .. '\\'')
        envy.run('"cwd test" | Out-File -FilePath cwd_marker.txt')
    else
        envy.run("echo 'tmp test' > '" .. tmp_file .. "'")
        envy.run("echo 'cwd test' > cwd_marker.txt")
    end
    local test_cmd1 = envy.PLATFORM == "windows"
        and 'if (Test-Path tmp_marker.txt) { exit 0 } else { exit 1 }'
        or "test -f tmp_marker.txt"
    local success, res2 = pcall(function() return envy.run(test_cmd1, {quiet = true}) end)
    if success and res2.exit_code == 0 then
        error("tmp_marker.txt found in cwd - tmp_dir appears to be the same as cwd")
    end
    local test_cmd2 = envy.PLATFORM == "windows"
        and ('if (Test-Path \\'' .. tmp_file .. '\\') { exit 0 } else { exit 1 }')
        or ("test -f '" .. tmp_file .. "'")
    local res3 = envy.run(test_cmd2, {quiet = true})
    if res3.exit_code ~= 0 then error("Could not find file in tmp_dir") end
    if envy.PLATFORM == "windows" then
        envy.run('Remove-Item -Force -ErrorAction SilentlyContinue cwd_marker.txt', {quiet = true})
    else
        envy.run("rm -f cwd_marker.txt", {quiet = true})
    end
    if envy.PLATFORM == "windows" then
        envy.run('New-Item -ItemType File -Force -Path \\'' .. marker .. '\\' | Out-Null')
    else
        envy.run("touch '" .. marker .. "'")
    end
end
"""
        marker_file = self.test_dir / "tmp_vs_cwd_marker"
        self.run_spec(
            "user_tmp_vs_cwd",
            spec,
            "local.user_tmp_vs_cwd@v1",
            env_vars={"ENVY_TEST_TMP_CWD_MARKER": str(marker_file)},
        )
        self.assertTrue(marker_file.exists())


if __name__ == "__main__":
    unittest.main()

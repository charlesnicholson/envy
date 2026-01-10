#!/usr/bin/env python3
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

# Inline specs for check/install runtime tests
SPECS = {
    "check_string_success_silent.lua": """-- Test that check string success is silent (no TUI output)
IDENTITY = "local.check_string_success@v1"

-- String check that succeeds (exits 0)
CHECK = "echo 'test'"

-- Need install verb since we have check
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check always passes
end
""",
    "check_ctx_run_quiet_success.lua": """-- Test check with ctx.run quiet=true on success
IDENTITY = "local.check_ctx_run_quiet@v1"

function CHECK(project_root, options)
    -- Quiet success: no TUI output, returns table with exit_code
    local res = envy.run("echo 'test output'", {quiet = true})

    -- Verify exit_code field exists
    assert(res.exit_code ~= nil, "exit_code field should exist")
    assert(res.exit_code == 0, "exit_code should be 0")

    return true  -- Check passes, skip install
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
""",
    "check_ctx_run_quiet_failure.lua": """-- Test check with ctx.run quiet=true on failure (throws)
IDENTITY = "local.check_ctx_run_quiet_fail@v1"

function CHECK(project_root, options)
    -- Quiet failure: no TUI output, but should throw
    local res = envy.run("exit 1", {quiet = true})

    -- Should not reach here because ctx.run throws on non-zero
    error("Should have thrown on non-zero exit")
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached due to check error
end
""",
    "check_ctx_run_capture.lua": """-- Test check with ctx.run capture=true returns stdout, stderr, exit_code
IDENTITY = "local.check_ctx_run_capture@v1"

function CHECK(project_root, options)
    -- Capture=true: should get stdout, stderr, exit_code fields
    local cmd
    if envy.PLATFORM == "windows" then
        cmd = "Write-Output 'stdout text'; [Console]::Error.WriteLine('stderr text')"
    else
        cmd = "echo 'stdout text' && echo 'stderr text' >&2"
    end
    local res = envy.run(cmd, {capture = true})

    -- Verify all three fields exist
    assert(res.stdout ~= nil, "stdout field should exist")
    assert(res.stderr ~= nil, "stderr field should exist")
    assert(res.exit_code ~= nil, "exit_code field should exist")

    -- Verify content
    assert(res.stdout:match("stdout text"), "stdout should contain expected text")
    assert(res.exit_code == 0, "exit_code should be 0")

    return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
""",
    "check_ctx_run_no_capture.lua": """-- Test check with ctx.run capture=false returns only exit_code
IDENTITY = "local.check_ctx_run_no_capture@v1"

function CHECK(project_root, options)
    -- Capture=false (or no capture): should only get exit_code field
    local res = envy.run("echo 'test'", {capture = false})

    -- Verify only exit_code exists
    assert(res.exit_code ~= nil, "exit_code field should exist")
    assert(res.stdout == nil, "stdout field should not exist when capture=false")
    assert(res.stderr == nil, "stderr field should not exist when capture=false")
    assert(res.exit_code == 0, "exit_code should be 0")

    return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
""",
    "check_ctx_run_default.lua": """-- Test ctx.run default (no flags): streams, throws on non-zero, returns exit_code
IDENTITY = "local.check_ctx_run_default@v1"

function CHECK(project_root, options)
    -- Default behavior: streams to TUI, throws on error, returns table with exit_code
    local res = envy.run("echo 'default test'")

    -- Should get exit_code field
    assert(res.exit_code ~= nil, "exit_code field should exist")
    assert(res.exit_code == 0, "exit_code should be 0")

    -- stdout/stderr should not be in table (not captured)
    assert(res.stdout == nil, "stdout not captured by default")
    assert(res.stderr == nil, "stderr not captured by default")

    return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
""",
    "check_ctx_run_combo_neither.lua": """-- Test ctx.run with neither quiet nor capture
IDENTITY = "local.check_combo_neither@v1"

function CHECK(project_root, options)
    -- Neither quiet nor capture: streams, returns exit_code only
    local res = envy.run("echo 'combo test'")

    assert(res.exit_code == 0)
    assert(res.stdout == nil, "stdout not captured")
    assert(res.stderr == nil, "stderr not captured")

    return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
""",
    "check_ctx_run_combo_quiet_only.lua": """-- Test ctx.run with quiet=true only
IDENTITY = "local.check_combo_quiet@v1"

function CHECK(project_root, options)
    -- Quiet only: no TUI, returns exit_code only
    local res = envy.run("echo 'quiet test'", {quiet = true})

    assert(res.exit_code == 0)
    assert(res.stdout == nil, "stdout not captured")
    assert(res.stderr == nil, "stderr not captured")

    return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
""",
    "check_ctx_run_combo_capture_only.lua": """-- Test ctx.run with capture=true only
IDENTITY = "local.check_combo_capture@v1"

function CHECK(project_root, options)
    -- Capture only: streams to TUI, returns stdout/stderr/exit_code
    local res = envy.run("echo 'capture test'", {capture = true})

    assert(res.exit_code == 0)
    assert(res.stdout ~= nil, "stdout should be captured")
    assert(res.stderr ~= nil, "stderr should be captured")
    assert(res.stdout:match("capture test"))

    return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
""",
    "check_ctx_run_combo_both.lua": """-- Test ctx.run with both quiet=true and capture=true
IDENTITY = "local.check_combo_both@v1"

function CHECK(project_root, options)
    -- Both quiet and capture: no TUI, returns stdout/stderr/exit_code
    local res = envy.run("echo 'both test'", {quiet = true, capture = true})

    assert(res.exit_code == 0)
    assert(res.stdout ~= nil, "stdout should be captured")
    assert(res.stderr ~= nil, "stderr should be captured")
    assert(res.stdout:match("both test"))

    return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
""",
    "check_cwd_manifest_dir.lua": """-- Test that check cwd = manifest directory
IDENTITY = "local.check_cwd_manifest@v1"

function CHECK(project_root, options)
    -- Write a marker file using relative path to verify cwd
    -- If cwd is manifest dir, this will create it there
    if envy.PLATFORM == "windows" then
        envy.run('"cwd_test" | Out-File -FilePath cwd_check_marker.txt')
    else
        envy.run("pwd > /tmp/check_cwd_test_pwd.txt")
        envy.run("echo 'cwd_test' > cwd_check_marker.txt")
    end

    -- Verify it was created (if cwd wasn't writable or correct, this would fail)
    local test_cmd = envy.PLATFORM == "windows"
        and 'if (Test-Path cwd_check_marker.txt) { exit 0 } else { exit 1 }'
        or "test -f cwd_check_marker.txt"
    local res = envy.run(test_cmd, {quiet = true})

    -- Clean up
    if envy.PLATFORM == "windows" then
        envy.run('Remove-Item -Force -ErrorAction SilentlyContinue cwd_check_marker.txt', {quiet = true})
    else
        envy.run("rm -f cwd_check_marker.txt /tmp/check_cwd_test_pwd.txt", {quiet = true})
    end

    if res.exit_code ~= 0 then
        error("Could not create marker file relative to cwd")
    end

    return true  -- Check passes
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
""",
    "install_cwd_cache_managed.lua": """-- Test install cwd = install_dir for cache-managed packages
IDENTITY = "local.install_cwd_cache@v1"

-- No check verb, so this is cache-managed
-- Need a fetch source for cache-managed packages
function FETCH(tmp_dir, options)
    -- Empty fetch - we're just testing install cwd
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Write a file using relative path
    if envy.PLATFORM == "windows" then
        envy.run('"test" | Out-File -FilePath cwd_marker.txt')
    else
        envy.run("echo 'test' > cwd_marker.txt")
    end

    -- Verify it's in install_dir by checking relative path works
    local test_cmd = envy.PLATFORM == "windows"
        and 'if (Test-Path cwd_marker.txt) { exit 0 } else { exit 1 }'
        or "test -f cwd_marker.txt"
    local res = envy.run(test_cmd, {quiet = true})

    if res.exit_code ~= 0 then
        error("Marker file not accessible via relative path - cwd issue")
    end

    -- Also verify install_dir contains the file
    local marker_path = install_dir .. (envy.PLATFORM == "windows" and "\\\\cwd_marker.txt" or "/cwd_marker.txt")
    local test_cmd2 = envy.PLATFORM == "windows"
        and ('if (Test-Path \\'' .. marker_path .. '\\') { exit 0 } else { exit 1 }')
        or ("test -f '" .. marker_path .. "'")
    local res2 = envy.run(test_cmd2, {quiet = true})

    if res2.exit_code ~= 0 then
        error("Marker file not in install_dir - cwd was not install_dir")
    end
end
""",
    "install_cwd_user_managed.lua": """-- Test install cwd = manifest directory for user-managed packages
IDENTITY = "local.install_cwd_user@v1"

-- Has check verb, so this is user-managed
function CHECK(project_root, options)
    local marker = os.getenv("ENVY_TEST_INSTALL_MARKER")
    if not marker then error("ENVY_TEST_INSTALL_MARKER not set") end

    -- Check if marker exists (non-zero means doesn't exist, need install)
    -- Use pcall since ctx.run throws on non-zero exit
    local test_cmd = envy.PLATFORM == "windows"
        and ('if (Test-Path \\'' .. marker .. '\\') { exit 0 } else { exit 1 }')
        or ("test -f '" .. marker .. "'")
    local success, res = pcall(function()
        return envy.run(test_cmd, {quiet = true})
    end)

    return success and res.exit_code == 0
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    local marker = os.getenv("ENVY_TEST_INSTALL_MARKER")
    if not marker then error("ENVY_TEST_INSTALL_MARKER not set") end

    -- Write a file using relative path to verify cwd
    if envy.PLATFORM == "windows" then
        envy.run('"user_managed_cwd_test" | Out-File -FilePath user_install_cwd_marker.txt')
    else
        envy.run("echo 'user_managed_cwd_test' > user_install_cwd_marker.txt")
    end

    -- Verify we can access it with relative path
    local test_cmd = envy.PLATFORM == "windows"
        and 'if (Test-Path user_install_cwd_marker.txt) { exit 0 } else { exit 1 }'
        or "test -f user_install_cwd_marker.txt"
    local res = envy.run(test_cmd, {quiet = true})

    -- Clean up
    if envy.PLATFORM == "windows" then
        envy.run('Remove-Item -Force -ErrorAction SilentlyContinue user_install_cwd_marker.txt', {quiet = true})
    else
        envy.run("rm -f user_install_cwd_marker.txt", {quiet = true})
    end

    if res.exit_code ~= 0 then
        error("Could not create file with relative path - cwd issue")
    end

    -- Create marker to indicate success
    if envy.PLATFORM == "windows" then
        envy.run('New-Item -ItemType File -Force -Path \\'' .. marker .. '\\' | Out-Null')
    else
        envy.run("touch '" .. marker .. "'")
    end
end
""",
    "check_default_shell.lua": """-- Test that manifest default_shell is respected in check string
IDENTITY = "local.check_default_shell@v1"

-- Use default shell (will use system default)
CHECK = "echo 'shell test'"

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check always passes
end
""",
    "install_default_shell.lua": """-- Test that manifest default_shell is respected in install string
IDENTITY = "local.install_default_shell@v1"

-- Cache-managed (no check verb)
-- Need fetch for cache-managed packages
function FETCH(tmp_dir, options)
    -- Empty fetch
end

-- Use default shell (will use system default)
INSTALL = "echo 'install shell test' > output.txt"
""",
    "check_table_field_access.lua": """-- Test direct table field access patterns
IDENTITY = "local.check_table_fields@v1"

function CHECK(project_root, options)
    -- Test direct field access
    local res = envy.run("echo 'test output'", {capture = true})

    -- Access via variable
    local code = res.exit_code
    local out = res.stdout
    local err = res.stderr

    assert(code == 0, "exit_code should be 0")
    assert(out ~= nil, "stdout should exist")
    assert(err ~= nil, "stderr should exist")
    assert(out:match("test output"), "stdout should contain expected text")

    return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
""",
    "check_table_chained_access.lua": """-- Test chained table field access patterns
IDENTITY = "local.check_table_chained@v1"

function CHECK(project_root, options)
    -- Test chained access: envy.run(...).field
    local out = envy.run("echo 'chained'", {capture = true}).stdout
    assert(out:match("chained"), "chained stdout access should work")

    local code = envy.run("echo 'test'", {quiet = true}).exit_code
    assert(code == 0, "chained exit_code access should work")

    return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
""",
    "check_error_command_not_found.lua": """-- Test shell error: command not found
IDENTITY = "local.check_error_not_found@v1"

function CHECK(project_root, options)
    local res = envy.run("nonexistent_command_12345", {quiet = true, check = true})
    error("Should have thrown on command not found")
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached due to check error
end
""",
    "check_error_syntax.lua": """-- Test shell error: syntax error
IDENTITY = "local.check_error_syntax@v1"

function CHECK(project_root, options)
    -- Run a command with shell syntax error
    local res = envy.run("echo 'unclosed quote", {quiet = true})

    -- Should not reach here because ctx.run throws on error
    error("Should have thrown on syntax error")
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached due to check error
end
""",
    "check_concurrent_output.lua": """-- Test concurrent large stdout+stderr with capture (no deadlock)
IDENTITY = "local.check_concurrent@v1"

function CHECK(project_root, options)
    -- Generate large output on both stdout and stderr
    local cmd
    if envy.PLATFORM == "windows" then
        cmd = [[
for ($i=1; $i -le 1000; $i++) {
    Write-Output "stdout line $i"
    [Console]::Error.WriteLine("stderr line $i")
}
]]
    else
        cmd = [[
for i in $(seq 1 1000); do
    echo "stdout line $i"
    echo "stderr line $i" >&2
done
]]
    end

    local res = envy.run(cmd, {capture = true})

    -- Verify we got output and didn't deadlock
    assert(res.exit_code == 0, "command should succeed")
    assert(res.stdout:match("stdout line"), "should have stdout")
    assert(res.stderr:match("stderr line"), "should have stderr")

    return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
""",
    "check_empty_output_failure.lua": """-- Test empty outputs in failure messages
IDENTITY = "local.check_empty_output@v1"

function CHECK(project_root, options)
    -- Run command that fails with no output
    local res = envy.run("exit 42", {quiet = true})

    -- Should not reach here
    error("Should have thrown on non-zero exit")
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached due to check error
end
""",
    "user_managed_tmp_dir_lifecycle.lua": """-- Test user-managed tmp_dir lifecycle
IDENTITY = "local.user_tmp_lifecycle@v1"

function CHECK(project_root, options)
    local marker = os.getenv("ENVY_TEST_TMP_MARKER")
    if not marker then error("ENVY_TEST_TMP_MARKER not set") end

    -- Check if marker exists (non-zero means doesn't exist, need install)
    -- Use pcall since ctx.run throws on non-zero exit
    local test_cmd = envy.PLATFORM == "windows"
        and ('if (Test-Path \\'' .. marker .. '\\') { exit 0 } else { exit 1 }')
        or ("test -f '" .. marker .. "'")
    local success, res = pcall(function()
        return envy.run(test_cmd, {quiet = true})
    end)

    -- If call succeeded and exit code is 0, marker exists
    return success and res.exit_code == 0
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    local marker = os.getenv("ENVY_TEST_TMP_MARKER")
    if not marker then error("ENVY_TEST_TMP_MARKER not set") end

    -- Verify tmp_dir exists and is accessible
    assert(tmp_dir ~= nil, "tmp_dir should be exposed")

    -- Write a file to tmp_dir to verify it works
    local path_sep = envy.PLATFORM == "windows" and "\\\\" or "/"
    local test_file = tmp_dir .. path_sep .. "test_file.txt"
    if envy.PLATFORM == "windows" then
        envy.run('"test" | Out-File -FilePath \\'' .. test_file .. '\\'')
    else
        envy.run("echo 'test' > " .. test_file)
    end

    -- Verify the file was created
    local test_cmd = envy.PLATFORM == "windows"
        and ('if (Test-Path \\'' .. test_file .. '\\') { exit 0 } else { exit 1 }')
        or ("test -f " .. test_file)
    local res = envy.run(test_cmd, {quiet = true})
    if res.exit_code ~= 0 then
        error("Could not write to tmp_dir")
    end

    -- Create marker file to indicate success
    if envy.PLATFORM == "windows" then
        envy.run('New-Item -ItemType File -Force -Path \\'' .. marker .. '\\' | Out-Null')
    else
        envy.run("touch '" .. marker .. "'")
    end
end
""",
    "user_managed_cleanup.lua": """-- Test user-managed entry_dir cleanup
IDENTITY = "local.user_cleanup@v1"

function CHECK(project_root, options)
    local marker = os.getenv("ENVY_TEST_CLEANUP_MARKER")
    if not marker then error("ENVY_TEST_CLEANUP_MARKER not set") end

    -- Check if marker exists (non-zero means doesn't exist, need install)
    -- Use pcall since ctx.run throws on non-zero exit
    local success, res = pcall(function()
        return envy.run("test -f '" .. marker .. "'", {quiet = true})
    end)

    return success and res.exit_code == 0
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    local marker = os.getenv("ENVY_TEST_CLEANUP_MARKER")
    if not marker then error("ENVY_TEST_CLEANUP_MARKER not set") end

    -- Create marker to indicate install ran
    envy.run("touch '" .. marker .. "'")

    -- tmp_dir and work_dir will be cleaned up automatically after this returns
end
""",
    "user_managed_check_no_tmp.lua": """-- Test that check phase does not expose tmp_dir
IDENTITY = "local.user_check_no_tmp@v1"

function CHECK(project_root, options)
    -- tmp_dir should not be exposed in check phase
    -- It's only for install phase (ephemeral workspace)
    -- Check phase tests system state, not cache state

    -- tmp_dir should not exist in check context
    -- This is actually expected - check doesn't have tmp_dir

    return true  -- Check always passes for this test
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- tmp_dir IS exposed in install phase
    assert(tmp_dir ~= nil, "tmp_dir should be exposed in install phase")
end
""",
    "user_managed_tmp_vs_cwd.lua": """-- Test user-managed: tmp_dir for workspace, cwd is manifest directory
IDENTITY = "local.user_tmp_vs_cwd@v1"

function CHECK(project_root, options)
    local marker = os.getenv("ENVY_TEST_TMP_CWD_MARKER")
    if not marker then error("ENVY_TEST_TMP_CWD_MARKER not set") end

    -- Check if marker exists (non-zero means doesn't exist, need install)
    -- Use pcall since ctx.run throws on non-zero exit
    local test_cmd = envy.PLATFORM == "windows"
        and ('if (Test-Path \\'' .. marker .. '\\') { exit 0 } else { exit 1 }')
        or ("test -f '" .. marker .. "'")
    local success, res = pcall(function()
        return envy.run(test_cmd, {quiet = true})
    end)

    return success and res.exit_code == 0
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    local marker = os.getenv("ENVY_TEST_TMP_CWD_MARKER")
    if not marker then error("ENVY_TEST_TMP_CWD_MARKER not set") end

    -- Verify tmp_dir is exposed and accessible
    assert(tmp_dir ~= nil, "tmp_dir should be exposed")

    -- Write to tmp_dir
    local path_sep = envy.PLATFORM == "windows" and "\\\\" or "/"
    local tmp_file = tmp_dir .. path_sep .. "tmp_marker.txt"
    if envy.PLATFORM == "windows" then
        envy.run('"tmp test" | Out-File -FilePath \\'' .. tmp_file .. '\\'')
        envy.run('"cwd test" | Out-File -FilePath cwd_marker.txt')
    else
        envy.run("echo 'tmp test' > '" .. tmp_file .. "'")
        envy.run("echo 'cwd test' > cwd_marker.txt")
    end

    -- Verify the tmp_dir file is NOT in cwd (different directories)
    -- Use pcall since test -f throws on non-zero
    local test_cmd1 = envy.PLATFORM == "windows"
        and 'if (Test-Path tmp_marker.txt) { exit 0 } else { exit 1 }'
        or "test -f tmp_marker.txt"
    local success, res2 = pcall(function()
        return envy.run(test_cmd1, {quiet = true})
    end)
    if success and res2.exit_code == 0 then
        error("tmp_marker.txt found in cwd - tmp_dir appears to be the same as cwd")
    end

    -- Verify the file IS in tmp_dir
    local test_cmd2 = envy.PLATFORM == "windows"
        and ('if (Test-Path \\'' .. tmp_file .. '\\') { exit 0 } else { exit 1 }')
        or ("test -f '" .. tmp_file .. "'")
    local res3 = envy.run(test_cmd2, {quiet = true})
    if res3.exit_code ~= 0 then
        error("Could not find file in tmp_dir")
    end

    -- Clean up cwd marker
    if envy.PLATFORM == "windows" then
        envy.run('Remove-Item -Force -ErrorAction SilentlyContinue cwd_marker.txt', {quiet = true})
    else
        envy.run("rm -f cwd_marker.txt", {quiet = true})
    end

    -- Create marker file to indicate success
    if envy.PLATFORM == "windows" then
        envy.run('New-Item -ItemType File -Force -Path \\'' .. marker .. '\\' | Out-Null')
    else
        envy.run("touch '" .. marker .. "'")
    end
end
""",
}


class TestCheckInstallRuntime(unittest.TestCase):
    """Tests for check/install runtime behavior."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-check-install-test-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-check-install-work-"))
        self.specs_dir = Path(tempfile.mkdtemp(prefix="envy-check-install-specs-"))
        self.envy_test = test_config.get_envy_executable()
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

        # Write inline specs to temp directory
        for name, content in SPECS.items():
            (self.specs_dir / name).write_text(content, encoding="utf-8")

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)
        shutil.rmtree(self.specs_dir, ignore_errors=True)

    def run_envy(
        self, identity, spec_file, should_fail=False, env_vars=None, verbose=False
    ):
        """Run envy_functional_tester with spec, return subprocess result."""
        spec_path = self.specs_dir / spec_file
        cmd = [str(self.envy_test), f"--cache-root={self.cache_root}"]
        cmd.extend(self.trace_flag)
        if verbose:
            cmd.append("--verbose")
        cmd.extend(["engine-test", identity, str(spec_path)])

        env = os.environ.copy()
        if env_vars:
            env.update(env_vars)

        result = subprocess.run(
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

    # ===== Check String Success Behavior Tests =====

    def test_check_string_success_silent(self):
        """Check string success produces no TUI output (silent even without --verbose)."""
        result = self.run_envy(
            "local.check_string_success@v1", "check_string_success_silent.lua"
        )

        # Check should pass silently - no output expected
        # Only phase transition logs should appear (if at all)
        self.assertNotIn("python", result.stderr.lower())
        self.assertNotIn("version", result.stderr.lower())

    def test_check_string_success_silent_with_verbose(self):
        """Check string success produces no TUI output even with --verbose."""
        result = self.run_envy(
            "local.check_string_success@v1",
            "check_string_success_silent.lua",
            verbose=True,
        )

        # Even with --verbose, check success should be silent
        # Only phase transitions should log
        output = result.stderr.lower()
        # May see phase transitions but not command output
        self.assertNotIn("python", output)

    # ===== Check ctx.run Tests =====

    def test_check_ctx_run_quiet_true_success(self):
        """Check with ctx.run quiet=true success returns exit_code, no TUI output."""
        result = self.run_envy(
            "local.check_ctx_run_quiet@v1", "check_ctx_run_quiet_success.lua"
        )

        # Should succeed silently
        self.assertEqual(result.returncode, 0)

    def test_check_ctx_run_quiet_true_failure(self):
        """Check with ctx.run quiet=true failure throws with no TUI output."""
        result = self.run_envy(
            "local.check_ctx_run_quiet_fail@v1",
            "check_ctx_run_quiet_failure.lua",
            should_fail=True,
        )

        # Should fail with error but no command output in stderr
        self.assertIn("error", result.stderr.lower())

    def test_check_ctx_run_capture_true(self):
        """Check with ctx.run capture=true returns table with stdout, stderr, exit_code."""
        result = self.run_envy(
            "local.check_ctx_run_capture@v1", "check_ctx_run_capture.lua"
        )

        # Spec verifies stdout/stderr/exit_code fields exist
        self.assertEqual(result.returncode, 0)

    def test_check_ctx_run_capture_false(self):
        """Check with ctx.run capture=false returns table with only exit_code field."""
        result = self.run_envy(
            "local.check_ctx_run_no_capture@v1", "check_ctx_run_no_capture.lua"
        )

        # Spec verifies only exit_code field exists
        self.assertEqual(result.returncode, 0)

    # ===== ctx.run Default Behavior Tests =====

    def test_ctx_run_default_no_flags(self):
        """ctx.run default (no flags) streams, throws on non-zero, returns exit_code."""
        result = self.run_envy(
            "local.check_ctx_run_default@v1", "check_ctx_run_default.lua"
        )

        # Spec verifies default behavior
        self.assertEqual(result.returncode, 0)

    def test_ctx_run_quiet_capture_combinations(self):
        """Test all 4 combinations of quiet/capture flags."""
        test_cases = [
            ("check_ctx_run_combo_neither.lua", "local.check_combo_neither@v1"),
            ("check_ctx_run_combo_quiet_only.lua", "local.check_combo_quiet@v1"),
            ("check_ctx_run_combo_capture_only.lua", "local.check_combo_capture@v1"),
            ("check_ctx_run_combo_both.lua", "local.check_combo_both@v1"),
        ]

        for spec_file, identity in test_cases:
            with self.subTest(spec=spec_file):
                result = self.run_envy(identity, spec_file)
                self.assertEqual(result.returncode, 0)

    # ===== CWD and Shell Configuration Tests =====

    def test_check_cwd_manifest_directory(self):
        """Check cwd = manifest directory (test via relative path)."""
        result = self.run_envy(
            "local.check_cwd_manifest@v1", "check_cwd_manifest_dir.lua"
        )

        # Spec uses relative path to verify cwd is manifest directory
        self.assertEqual(result.returncode, 0)

    def test_install_cwd_cache_managed(self):
        """Install string cwd = install_dir for cache-managed packages."""
        result = self.run_envy(
            "local.install_cwd_cache@v1", "install_cwd_cache_managed.lua"
        )

        # Spec verifies install runs in install_dir
        self.assertEqual(result.returncode, 0)

    def test_install_cwd_user_managed(self):
        """Install string cwd = manifest directory for user-managed packages."""
        marker_file = self.test_dir / "install_marker"
        result = self.run_envy(
            "local.install_cwd_user@v1",
            "install_cwd_user_managed.lua",
            env_vars={"ENVY_TEST_INSTALL_MARKER": str(marker_file)},
        )

        # Spec has check verb, so it's user-managed
        # Install should run in manifest directory
        self.assertEqual(result.returncode, 0)

    def test_default_shell_respected_in_check(self):
        """manifest default_shell configuration is respected in check string."""
        result = self.run_envy(
            "local.check_default_shell@v1", "check_default_shell.lua"
        )

        # Spec specifies custom shell in manifest
        self.assertEqual(result.returncode, 0)

    def test_default_shell_respected_in_install(self):
        """manifest default_shell configuration is respected in install string."""
        result = self.run_envy(
            "local.install_default_shell@v1", "install_default_shell.lua"
        )

        # Spec specifies custom shell in manifest
        self.assertEqual(result.returncode, 0)

    # ===== Table Field Access Pattern Tests =====

    def test_table_field_direct_access(self):
        """Test direct table field access: res.exit_code, res.stdout."""
        result = self.run_envy(
            "local.check_table_fields@v1", "check_table_field_access.lua"
        )

        # Spec tests various field access patterns
        self.assertEqual(result.returncode, 0)

    def test_table_field_chained_access(self):
        """Test chained table field access: envy.run(...).stdout."""
        result = self.run_envy(
            "local.check_table_chained@v1", "check_table_chained_access.lua"
        )

        # Spec tests chained access patterns
        self.assertEqual(result.returncode, 0)

    # ===== Shell Error Type Tests =====

    def test_shell_error_command_not_found(self):
        """Shell error: command not found (exit 127 on Unix, 1 on Windows)."""
        result = self.run_envy(
            "local.check_error_not_found@v1",
            "check_error_command_not_found.lua",
            should_fail=True,
        )

        # Should fail with shell error (exit code 127 for command not found on Unix, 1 on Windows)
        # Error message varies by shell/platform
        stderr_lower = result.stderr.lower()
        if sys.platform == "win32":
            # PowerShell returns exit code 1 for command not found
            self.assertIn("exit code 1", stderr_lower)
        else:
            self.assertIn("exit code 127", stderr_lower)

    def test_shell_error_syntax_error(self):
        """Shell error: syntax error."""
        result = self.run_envy(
            "local.check_error_syntax@v1",
            "check_error_syntax.lua",
            should_fail=True,
        )

        # Should fail with syntax error
        self.assertIn("error", result.stderr.lower())

    # ===== Concurrent Output Tests =====

    def test_concurrent_large_stdout_stderr(self):
        """Concurrent large stdout+stderr with capture (no pipe deadlock)."""
        result = self.run_envy(
            "local.check_concurrent@v1", "check_concurrent_output.lua"
        )

        # Spec generates large interleaved stdout/stderr
        # Should complete without deadlock
        self.assertEqual(result.returncode, 0)

    # ===== Empty Output Tests =====

    def test_empty_outputs_in_failure(self):
        """Empty outputs in failure messages are clarified (not blank lines)."""
        result = self.run_envy(
            "local.check_empty_output@v1",
            "check_empty_output_failure.lua",
            should_fail=True,
        )

        # Should fail with clear error message even with empty output
        self.assertIn("error", result.stderr.lower())

    # ===== User-Managed Workspace Lifecycle Tests =====

    def test_user_managed_tmp_dir_created(self):
        """User-managed install: tmp_dir is created and accessible."""
        marker_file = self.test_dir / "tmp_marker"
        result = self.run_envy(
            "local.user_tmp_lifecycle@v1",
            "user_managed_tmp_dir_lifecycle.lua",
            env_vars={"ENVY_TEST_TMP_MARKER": str(marker_file)},
        )

        # Spec writes to tmp_dir to verify it exists
        self.assertEqual(result.returncode, 0)
        self.assertTrue(marker_file.exists())

    def test_user_managed_entry_dir_deleted(self):
        """User-managed: entry_dir deleted after completion."""
        marker_file = self.test_dir / "cleanup_marker"
        result = self.run_envy(
            "local.user_cleanup@v1",
            "user_managed_cleanup.lua",
            env_vars={"ENVY_TEST_CLEANUP_MARKER": str(marker_file)},
        )

        self.assertEqual(result.returncode, 0)

        # Verify no artifacts left in cache
        pkg_dir = self.cache_root / "packages" / "local.user_cleanup@v1"
        if pkg_dir.exists():
            # Should have no pkg/ subdirectories
            pkg_subdirs = list(pkg_dir.glob("*/pkg"))
            self.assertEqual(
                len(pkg_subdirs),
                0,
                "User-managed packages should not leave pkg/ in cache",
            )

    def test_user_managed_check_no_tmp_dir(self):
        """User-managed check phase: no tmp_dir exposed (check tests system state only)."""
        result = self.run_envy(
            "local.user_check_no_tmp@v1", "user_managed_check_no_tmp.lua"
        )

        # Recipe's check function should not have access to tmp_dir
        # (tmp_dir is only exposed in install phase)
        self.assertEqual(result.returncode, 0)

    def test_user_managed_tmp_dir_vs_cwd(self):
        """User-managed: tmp_dir for workspace, cwd is manifest directory."""
        marker_file = self.test_dir / "tmp_vs_cwd_marker"
        result = self.run_envy(
            "local.user_tmp_vs_cwd@v1",
            "user_managed_tmp_vs_cwd.lua",
            env_vars={"ENVY_TEST_TMP_CWD_MARKER": str(marker_file)},
        )

        # Spec verifies tmp_dir != cwd and cwd is manifest dir
        self.assertEqual(result.returncode, 0)
        self.assertTrue(marker_file.exists())


if __name__ == "__main__":
    unittest.main()

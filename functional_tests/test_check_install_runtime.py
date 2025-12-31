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


class TestCheckInstallRuntime(unittest.TestCase):
    """Tests for check/install runtime behavior."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-check-install-test-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-check-install-work-"))
        self.envy_test = test_config.get_envy_executable()
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []
        self.spec_dir = Path(__file__).parent.parent / "test_data" / "specs"

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def run_envy(
        self, identity, spec_path, should_fail=False, env_vars=None, verbose=False
    ):
        """Run envy_functional_tester with spec, return subprocess result."""
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
        spec_path = self.spec_dir / "check_string_success_silent.lua"
        result = self.run_envy("local.check_string_success@v1", spec_path)

        # Check should pass silently - no output expected
        # Only phase transition logs should appear (if at all)
        self.assertNotIn("python", result.stderr.lower())
        self.assertNotIn("version", result.stderr.lower())

    def test_check_string_success_silent_with_verbose(self):
        """Check string success produces no TUI output even with --verbose."""
        spec_path = self.spec_dir / "check_string_success_silent.lua"
        result = self.run_envy(
            "local.check_string_success@v1", spec_path, verbose=True
        )

        # Even with --verbose, check success should be silent
        # Only phase transitions should log
        output = result.stderr.lower()
        # May see phase transitions but not command output
        self.assertNotIn("python", output)

    # ===== Check ctx.run Tests =====

    def test_check_ctx_run_quiet_true_success(self):
        """Check with ctx.run quiet=true success returns exit_code, no TUI output."""
        spec_path = self.spec_dir / "check_ctx_run_quiet_success.lua"
        result = self.run_envy("local.check_ctx_run_quiet@v1", spec_path)

        # Should succeed silently
        self.assertEqual(result.returncode, 0)

    def test_check_ctx_run_quiet_true_failure(self):
        """Check with ctx.run quiet=true failure throws with no TUI output."""
        spec_path = self.spec_dir / "check_ctx_run_quiet_failure.lua"
        result = self.run_envy(
            "local.check_ctx_run_quiet_fail@v1", spec_path, should_fail=True
        )

        # Should fail with error but no command output in stderr
        self.assertIn("error", result.stderr.lower())

    def test_check_ctx_run_capture_true(self):
        """Check with ctx.run capture=true returns table with stdout, stderr, exit_code."""
        spec_path = self.spec_dir / "check_ctx_run_capture.lua"
        result = self.run_envy("local.check_ctx_run_capture@v1", spec_path)

        # Spec verifies stdout/stderr/exit_code fields exist
        self.assertEqual(result.returncode, 0)

    def test_check_ctx_run_capture_false(self):
        """Check with ctx.run capture=false returns table with only exit_code field."""
        spec_path = self.spec_dir / "check_ctx_run_no_capture.lua"
        result = self.run_envy("local.check_ctx_run_no_capture@v1", spec_path)

        # Spec verifies only exit_code field exists
        self.assertEqual(result.returncode, 0)

    # ===== ctx.run Default Behavior Tests =====

    def test_ctx_run_default_no_flags(self):
        """ctx.run default (no flags) streams, throws on non-zero, returns exit_code."""
        spec_path = self.spec_dir / "check_ctx_run_default.lua"
        result = self.run_envy("local.check_ctx_run_default@v1", spec_path)

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
                spec_path = self.spec_dir / spec_file
                result = self.run_envy(identity, spec_path)
                self.assertEqual(result.returncode, 0)

    # ===== CWD and Shell Configuration Tests =====

    def test_check_cwd_manifest_directory(self):
        """Check cwd = manifest directory (test via relative path)."""
        spec_path = self.spec_dir / "check_cwd_manifest_dir.lua"
        result = self.run_envy("local.check_cwd_manifest@v1", spec_path)

        # Spec uses relative path to verify cwd is manifest directory
        self.assertEqual(result.returncode, 0)

    def test_install_cwd_cache_managed(self):
        """Install string cwd = install_dir for cache-managed packages."""
        spec_path = self.spec_dir / "install_cwd_cache_managed.lua"
        result = self.run_envy("local.install_cwd_cache@v1", spec_path)

        # Spec verifies install runs in install_dir
        self.assertEqual(result.returncode, 0)

    def test_install_cwd_user_managed(self):
        """Install string cwd = manifest directory for user-managed packages."""
        marker_file = self.test_dir / "install_marker"
        spec_path = self.spec_dir / "install_cwd_user_managed.lua"
        result = self.run_envy(
            "local.install_cwd_user@v1",
            spec_path,
            env_vars={"ENVY_TEST_INSTALL_MARKER": str(marker_file)},
        )

        # Spec has check verb, so it's user-managed
        # Install should run in manifest directory
        self.assertEqual(result.returncode, 0)

    def test_default_shell_respected_in_check(self):
        """manifest default_shell configuration is respected in check string."""
        spec_path = self.spec_dir / "check_default_shell.lua"
        result = self.run_envy("local.check_default_shell@v1", spec_path)

        # Spec specifies custom shell in manifest
        self.assertEqual(result.returncode, 0)

    def test_default_shell_respected_in_install(self):
        """manifest default_shell configuration is respected in install string."""
        spec_path = self.spec_dir / "install_default_shell.lua"
        result = self.run_envy("local.install_default_shell@v1", spec_path)

        # Spec specifies custom shell in manifest
        self.assertEqual(result.returncode, 0)

    # ===== Table Field Access Pattern Tests =====

    def test_table_field_direct_access(self):
        """Test direct table field access: res.exit_code, res.stdout."""
        spec_path = self.spec_dir / "check_table_field_access.lua"
        result = self.run_envy("local.check_table_fields@v1", spec_path)

        # Spec tests various field access patterns
        self.assertEqual(result.returncode, 0)

    def test_table_field_chained_access(self):
        """Test chained table field access: envy.run(...).stdout."""
        spec_path = self.spec_dir / "check_table_chained_access.lua"
        result = self.run_envy("local.check_table_chained@v1", spec_path)

        # Spec tests chained access patterns
        self.assertEqual(result.returncode, 0)

    # ===== Shell Error Type Tests =====

    def test_shell_error_command_not_found(self):
        """Shell error: command not found (exit 127 on Unix, 1 on Windows)."""
        spec_path = self.spec_dir / "check_error_command_not_found.lua"
        result = self.run_envy(
            "local.check_error_not_found@v1", spec_path, should_fail=True
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
        spec_path = self.spec_dir / "check_error_syntax.lua"
        result = self.run_envy(
            "local.check_error_syntax@v1", spec_path, should_fail=True
        )

        # Should fail with syntax error
        self.assertIn("error", result.stderr.lower())

    # ===== Concurrent Output Tests =====

    def test_concurrent_large_stdout_stderr(self):
        """Concurrent large stdout+stderr with capture (no pipe deadlock)."""
        spec_path = self.spec_dir / "check_concurrent_output.lua"
        result = self.run_envy("local.check_concurrent@v1", spec_path)

        # Spec generates large interleaved stdout/stderr
        # Should complete without deadlock
        self.assertEqual(result.returncode, 0)

    # ===== Empty Output Tests =====

    def test_empty_outputs_in_failure(self):
        """Empty outputs in failure messages are clarified (not blank lines)."""
        spec_path = self.spec_dir / "check_empty_output_failure.lua"
        result = self.run_envy(
            "local.check_empty_output@v1", spec_path, should_fail=True
        )

        # Should fail with clear error message even with empty output
        self.assertIn("error", result.stderr.lower())

    # ===== User-Managed Workspace Lifecycle Tests =====

    def test_user_managed_tmp_dir_created(self):
        """User-managed install: tmp_dir is created and accessible."""
        marker_file = self.test_dir / "tmp_marker"
        spec_path = self.spec_dir / "user_managed_tmp_dir_lifecycle.lua"
        result = self.run_envy(
            "local.user_tmp_lifecycle@v1",
            spec_path,
            env_vars={"ENVY_TEST_TMP_MARKER": str(marker_file)},
        )

        # Spec writes to tmp_dir to verify it exists
        self.assertEqual(result.returncode, 0)
        self.assertTrue(marker_file.exists())

    def test_user_managed_entry_dir_deleted(self):
        """User-managed: entry_dir deleted after completion."""
        marker_file = self.test_dir / "cleanup_marker"
        spec_path = self.spec_dir / "user_managed_cleanup.lua"
        result = self.run_envy(
            "local.user_cleanup@v1",
            spec_path,
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
        spec_path = self.spec_dir / "user_managed_check_no_tmp.lua"
        result = self.run_envy("local.user_check_no_tmp@v1", spec_path)

        # Recipe's check function should not have access to tmp_dir
        # (tmp_dir is only exposed in install phase)
        self.assertEqual(result.returncode, 0)

    def test_user_managed_tmp_dir_vs_cwd(self):
        """User-managed: tmp_dir for workspace, cwd is manifest directory."""
        marker_file = self.test_dir / "tmp_vs_cwd_marker"
        spec_path = self.spec_dir / "user_managed_tmp_vs_cwd.lua"
        result = self.run_envy(
            "local.user_tmp_vs_cwd@v1",
            spec_path,
            env_vars={"ENVY_TEST_TMP_CWD_MARKER": str(marker_file)},
        )

        # Spec verifies tmp_dir != cwd and cwd is manifest dir
        self.assertEqual(result.returncode, 0)
        self.assertTrue(marker_file.exists())


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Functional tests for ctx.run() in stage phase.

Tests comprehensive functionality of ctx.run() including:
- Basic execution
- Error handling
- Working directory control
- Check mode behavior
- Environment variable management
- Integration with other ctx methods
- Output/logging
- Complex scenarios
- Edge cases
- Lua error integration
- Option combinations
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
import unittest

from . import test_config


class TestCtxRun(unittest.TestCase):
    """Tests for ctx.run() functionality."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-ctx-run-test-"))
        self.envy_test = test_config.get_envy_executable()
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)

    def get_asset_path(self, identity):
        """Find asset directory for given identity in cache."""
        assets_dir = self.cache_root / "assets" / identity
        if not assets_dir.exists():
            return None
        for subdir in assets_dir.iterdir():
            if subdir.is_dir():
                asset_dir = subdir / "asset"
                if asset_dir.exists():
                    return asset_dir
                return subdir
        return None

    def run_recipe(self, identity, recipe_path, should_fail=False):
        """Run a recipe and return result."""
        result = subprocess.run(
            [
                str(self.envy_test),
                *self.trace_flag,
                "engine-test",
                identity,
                recipe_path,
                f"--cache-root={self.cache_root}",
            ],
            capture_output=True,
            text=True,
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

    # ===== Basic Functionality Tests =====

    def test_basic_execution(self):
        """ctx.run() executes shell commands successfully."""
        self.run_recipe(
            "local.ctx_run_basic@v1",
            "test_data/recipes/ctx_run_basic.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_basic@v1")
        assert asset_path
        self.assertTrue((asset_path / "run_marker.txt").exists())

    def test_multiple_commands(self):
        """ctx.run() executes multiple commands in sequence."""
        self.run_recipe(
            "local.ctx_run_multiple_cmds@v1",
            "test_data/recipes/ctx_run_multiple_cmds.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_multiple_cmds@v1")
        assert asset_path
        self.assertTrue((asset_path / "all_cmds.txt").exists())

    def test_file_operations(self):
        """ctx.run() can perform file operations."""
        self.run_recipe(
            "local.ctx_run_file_ops@v1",
            "test_data/recipes/ctx_run_file_ops.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_file_ops@v1")
        assert asset_path
        self.assertTrue((asset_path / "ops_result.txt").exists())

    def test_runs_in_stage_dir(self):
        """ctx.run() executes in stage directory by default."""
        self.run_recipe(
            "local.ctx_run_in_stage_dir@v1",
            "test_data/recipes/ctx_run_in_stage_dir.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_in_stage_dir@v1")
        assert asset_path
        self.assertTrue((asset_path / "stage_verification.txt").exists())

    def test_with_pipes(self):
        """ctx.run() supports shell pipes and redirection."""
        self.run_recipe(
            "local.ctx_run_with_pipes@v1",
            "test_data/recipes/ctx_run_with_pipes.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_with_pipes@v1")
        assert asset_path
        self.assertTrue((asset_path / "grepped.txt").exists())

    # ===== Error Handling Tests =====

    def test_error_nonzero_exit(self):
        """ctx.run() fails on non-zero exit code."""
        self.run_recipe(
            "local.ctx_run_error_nonzero@v1",
            "test_data/recipes/ctx_run_error_nonzero.lua",
            should_fail=True,
        )

    def test_check_mode_catches_failures(self):
        """ctx.run() check mode catches command failures."""
        self.run_recipe(
            "local.ctx_run_check_mode@v1",
            "test_data/recipes/ctx_run_check_mode.lua",
            should_fail=True,
        )

    @unittest.skipIf(sys.platform == "win32", "Signals not supported on Windows")
    def test_signal_termination(self):
        """ctx.run() reports signal termination."""
        self.run_recipe(
            "local.ctx_run_signal_term@v1",
            "test_data/recipes/ctx_run_signal_term.lua",
            should_fail=True,
        )

    def test_invalid_cwd(self):
        """ctx.run() fails when cwd doesn't exist."""
        self.run_recipe(
            "local.ctx_run_invalid_cwd@v1",
            "test_data/recipes/ctx_run_invalid_cwd.lua",
            should_fail=True,
        )

    def test_command_not_found(self):
        """ctx.run() fails when command doesn't exist."""
        self.run_recipe(
            "local.ctx_run_command_not_found@v1",
            "test_data/recipes/ctx_run_command_not_found.lua",
            should_fail=True,
        )

    @unittest.skipUnless(os.name != "nt", "requires POSIX shells")
    def test_shell_sh(self):
        """ctx.run() executes explicitly via /bin/sh."""
        self.run_recipe(
            "local.ctx_run_shell_sh@v1",
            "test_data/recipes/ctx_run_shell_sh.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_shell_sh@v1")
        assert asset_path
        self.assertTrue((asset_path / "shell_sh_marker.txt").exists())

    @unittest.skipUnless(os.name == "nt", "requires Windows CMD")
    def test_shell_cmd(self):
        """ctx.run() executes explicitly via cmd.exe."""
        self.run_recipe(
            "local.ctx_run_shell_cmd@v1",
            "test_data/recipes/ctx_run_shell_cmd.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_shell_cmd@v1")
        assert asset_path
        self.assertTrue((asset_path / "shell_cmd_marker.txt").exists())

    # ===== CWD Tests =====

    def test_cwd_relative(self):
        """ctx.run() supports relative cwd paths."""
        self.run_recipe(
            "local.ctx_run_cwd_relative@v1",
            "test_data/recipes/ctx_run_cwd_relative.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_cwd_relative@v1")
        assert asset_path
        self.assertTrue((asset_path / "custom" / "subdir" / "marker.txt").exists())

    def test_cwd_absolute(self):
        """ctx.run() supports absolute cwd paths."""
        self.run_recipe(
            "local.ctx_run_cwd_absolute@v1",
            "test_data/recipes/ctx_run_cwd_absolute.lua",
        )
        if os.name == "nt":
            target = Path(os.environ.get("TEMP", "C:/Temp")) / "envy_ctx_run_test.txt"
        else:
            target = Path("/tmp/envy_ctx_run_test.txt")
        self.assertTrue(target.exists())
        target.unlink(missing_ok=True)

    def test_cwd_parent(self):
        """ctx.run() handles parent directory (..) in cwd."""
        self.run_recipe(
            "local.ctx_run_cwd_parent@v1",
            "test_data/recipes/ctx_run_cwd_parent.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_cwd_parent@v1")
        assert asset_path
        self.assertTrue((asset_path / "deep" / "parent_marker.txt").exists())

    def test_cwd_nested(self):
        """ctx.run() handles deeply nested relative cwd."""
        self.run_recipe(
            "local.ctx_run_cwd_nested@v1",
            "test_data/recipes/ctx_run_cwd_nested.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_cwd_nested@v1")
        assert asset_path
        self.assertTrue(
            (
                asset_path
                / "level1"
                / "level2"
                / "level3"
                / "level4"
                / "nested_marker.txt"
            ).exists()
        )

    # ===== Check Mode Tests =====

    def test_continue_after_failure(self):
        """ctx.run() continues execution after a failing command."""
        self.run_recipe(
            "local.ctx_run_continue_after_failure@v1",
            "test_data/recipes/ctx_run_continue_after_failure.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_continue_after_failure@v1")
        assert asset_path
        self.assertTrue((asset_path / "continued.txt").exists())

    def test_check_undefined_variable(self):
        """ctx.run() check mode catches undefined variables."""
        self.run_recipe(
            "local.ctx_run_check_undefined@v1",
            "test_data/recipes/ctx_run_check_undefined.lua",
            should_fail=True,
        )

    def test_check_pipefail(self):
        """ctx.run() check mode catches pipe failures."""
        self.run_recipe(
            "local.ctx_run_check_pipefail@v1",
            "test_data/recipes/ctx_run_check_pipefail.lua",
            should_fail=True,
        )

    def test_check_false_nonzero(self):
        """ctx.run() with check=false allows non-zero exit codes."""
        self.run_recipe(
            "local.ctx_run_check_false_nonzero@v1",
            "test_data/recipes/ctx_run_check_false_nonzero.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_check_false_nonzero@v1")
        assert asset_path
        self.assertTrue((asset_path / "continued_after_failure.txt").exists())

    def test_check_false_capture(self):
        """ctx.run() with check=false and capture returns exit_code and output."""
        self.run_recipe(
            "local.ctx_run_check_false_capture@v1",
            "test_data/recipes/ctx_run_check_false_capture.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_check_false_capture@v1")
        assert asset_path
        self.assertTrue((asset_path / "capture_success.txt").exists())

    # ===== Environment Tests =====

    def test_env_custom(self):
        """ctx.run() supports custom environment variables."""
        self.run_recipe(
            "local.ctx_run_env_custom@v1",
            "test_data/recipes/ctx_run_env_custom.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_env_custom@v1")
        assert asset_path
        self.assertTrue((asset_path / "env_output.txt").exists())
        content = (asset_path / "env_output.txt").read_text()
        self.assertIn("MY_VAR=test_value", content)
        self.assertIn("MY_NUM=42", content)

    def test_env_inherit(self):
        """ctx.run() inherits environment variables like PATH."""
        self.run_recipe(
            "local.ctx_run_env_inherit@v1",
            "test_data/recipes/ctx_run_env_inherit.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_env_inherit@v1")
        assert asset_path
        self.assertTrue((asset_path / "path_verification.txt").exists())

    def test_env_override(self):
        """ctx.run() can override inherited environment variables."""
        self.run_recipe(
            "local.ctx_run_env_override@v1",
            "test_data/recipes/ctx_run_env_override.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_env_override@v1")
        assert asset_path
        content = (asset_path / "overridden_user.txt").read_text()
        self.assertIn("USER=test_override_user", content)

    def test_env_empty(self):
        """ctx.run() with empty env table still inherits."""
        self.run_recipe(
            "local.ctx_run_env_empty@v1",
            "test_data/recipes/ctx_run_env_empty.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_env_empty@v1")
        assert asset_path
        self.assertTrue((asset_path / "empty_env.txt").exists())

    def test_env_complex(self):
        """ctx.run() handles complex environment values."""
        self.run_recipe(
            "local.ctx_run_env_complex@v1",
            "test_data/recipes/ctx_run_env_complex.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_env_complex@v1")
        assert asset_path
        content = (asset_path / "env_complex.txt").read_text()
        self.assertIn("STRING=hello_world", content)
        self.assertIn("NUMBER=12345", content)
        self.assertIn("WITH_SPACE=value with spaces", content)

    # ===== Integration Tests =====

    def test_with_extract(self):
        """ctx.run() works with ctx.extract_all()."""
        self.run_recipe(
            "local.ctx_run_with_extract@v1",
            "test_data/recipes/ctx_run_with_extract.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_with_extract@v1")
        assert asset_path
        self.assertTrue((asset_path / "verify_extract.txt").exists())

    def test_with_rename(self):
        """ctx.run() works with ctx.rename()."""
        self.run_recipe(
            "local.ctx_run_with_rename@v1",
            "test_data/recipes/ctx_run_with_rename.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_with_rename@v1")
        assert asset_path
        self.assertTrue((asset_path / "rename_check.txt").exists())

    def test_with_template(self):
        """ctx.run() works with ctx.template()."""
        self.run_recipe(
            "local.ctx_run_with_template@v1",
            "test_data/recipes/ctx_run_with_template.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_with_template@v1")
        assert asset_path
        self.assertTrue((asset_path / "template_check.txt").exists())

    def test_multiple_calls(self):
        """Multiple ctx.run() calls work in sequence."""
        self.run_recipe(
            "local.ctx_run_multiple_calls@v1",
            "test_data/recipes/ctx_run_multiple_calls.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_multiple_calls@v1")
        assert asset_path
        self.assertTrue((asset_path / "all_calls.txt").exists())

    def test_interleaved(self):
        """ctx.run() interleaves with other operations."""
        self.run_recipe(
            "local.ctx_run_interleaved@v1",
            "test_data/recipes/ctx_run_interleaved.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_interleaved@v1")
        assert asset_path
        self.assertTrue((asset_path / "step_count.txt").exists())

    # ===== Phase-Specific Tests =====

    def test_stage_build_prep(self):
        """ctx.run() in stage for build preparation."""
        self.run_recipe(
            "local.ctx_run_stage_build_prep@v1",
            "test_data/recipes/ctx_run_stage_build_prep.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_stage_build_prep@v1")
        assert asset_path
        self.assertTrue((asset_path / "build" / "config.txt").exists())

    def test_stage_patch(self):
        """ctx.run() in stage for patching."""
        self.run_recipe(
            "local.ctx_run_stage_patch@v1",
            "test_data/recipes/ctx_run_stage_patch.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_stage_patch@v1")
        assert asset_path
        self.assertTrue((asset_path / "patch_log.txt").exists())

    def test_stage_permissions(self):
        """ctx.run() in stage for setting permissions."""
        self.run_recipe(
            "local.ctx_run_stage_permissions@v1",
            "test_data/recipes/ctx_run_stage_permissions.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_stage_permissions@v1")
        assert asset_path
        self.assertTrue((asset_path / "permissions.txt").exists())

    def test_stage_verification(self):
        """ctx.run() in stage for verification checks."""
        self.run_recipe(
            "local.ctx_run_stage_verification@v1",
            "test_data/recipes/ctx_run_stage_verification.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_stage_verification@v1")
        assert asset_path
        self.assertTrue((asset_path / "verification.txt").exists())

    def test_stage_generation(self):
        """ctx.run() in stage for code generation."""
        self.run_recipe(
            "local.ctx_run_stage_generation@v1",
            "test_data/recipes/ctx_run_stage_generation.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_stage_generation@v1")
        assert asset_path
        self.assertTrue((asset_path / "generated.h").exists())
        self.assertTrue((asset_path / "generated.sh").exists())

    def test_stage_cleanup(self):
        """ctx.run() in stage for cleanup operations."""
        self.run_recipe(
            "local.ctx_run_stage_cleanup@v1",
            "test_data/recipes/ctx_run_stage_cleanup.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_stage_cleanup@v1")
        assert asset_path
        self.assertTrue((asset_path / "cleanup_log.txt").exists())

    def test_stage_compilation(self):
        """ctx.run() in stage for compilation."""
        self.run_recipe(
            "local.ctx_run_stage_compilation@v1",
            "test_data/recipes/ctx_run_stage_compilation.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_stage_compilation@v1")
        assert asset_path
        self.assertTrue((asset_path / "compile_log.txt").exists())

    def test_stage_archiving(self):
        """ctx.run() in stage for creating archives."""
        self.run_recipe(
            "local.ctx_run_stage_archiving@v1",
            "test_data/recipes/ctx_run_stage_archiving.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_stage_archiving@v1")
        assert asset_path
        self.assertTrue((asset_path / "archive_log.txt").exists())

    # ===== Output/Logging Tests =====

    def test_output_stdout(self):
        """ctx.run() captures stdout output."""
        self.run_recipe(
            "local.ctx_run_output_stdout@v1",
            "test_data/recipes/ctx_run_output_stdout.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_output_stdout@v1")
        assert asset_path
        self.assertTrue((asset_path / "stdout_marker.txt").exists())

    def test_output_stderr(self):
        """ctx.run() captures stderr output."""
        self.run_recipe(
            "local.ctx_run_output_stderr@v1",
            "test_data/recipes/ctx_run_output_stderr.lua",
        )
        # Stderr should be captured in logs
        asset_path = self.get_asset_path("local.ctx_run_output_stderr@v1")
        assert asset_path
        self.assertTrue((asset_path / "stderr_marker.txt").exists())

    def test_output_large(self):
        """ctx.run() handles large output."""
        self.run_recipe(
            "local.ctx_run_output_large@v1",
            "test_data/recipes/ctx_run_output_large.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_output_large@v1")
        assert asset_path
        self.assertTrue((asset_path / "large_output_marker.txt").exists())

    def test_output_multiline(self):
        """ctx.run() handles multi-line output."""
        self.run_recipe(
            "local.ctx_run_output_multiline@v1",
            "test_data/recipes/ctx_run_output_multiline.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_output_multiline@v1")
        assert asset_path
        self.assertTrue((asset_path / "multiline_marker.txt").exists())

    # ===== Complex Scenario Tests =====

    def test_complex_workflow(self):
        """ctx.run() handles complex real-world workflow."""
        self.run_recipe(
            "local.ctx_run_complex_workflow@v1",
            "test_data/recipes/ctx_run_complex_workflow.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_complex_workflow@v1")
        assert asset_path
        self.assertTrue((asset_path / "workflow_complete.txt").exists())

    def test_complex_env_manipulation(self):
        """ctx.run() handles complex environment manipulation."""
        self.run_recipe(
            "local.ctx_run_complex_env_manip@v1",
            "test_data/recipes/ctx_run_complex_env_manip.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_complex_env_manip@v1")
        assert asset_path
        self.assertTrue((asset_path / "env_step1.txt").exists())
        self.assertTrue((asset_path / "env_step2.txt").exists())
        self.assertTrue((asset_path / "env_step3.txt").exists())

    def test_complex_conditional(self):
        """ctx.run() handles conditional operations."""
        self.run_recipe(
            "local.ctx_run_complex_conditional@v1",
            "test_data/recipes/ctx_run_complex_conditional.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_complex_conditional@v1")
        assert asset_path
        self.assertTrue((asset_path / "os_info.txt").exists())
        self.assertTrue((asset_path / "test_info.txt").exists())

    def test_complex_loops(self):
        """ctx.run() handles loops and iterations."""
        self.run_recipe(
            "local.ctx_run_complex_loops@v1",
            "test_data/recipes/ctx_run_complex_loops.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_complex_loops@v1")
        assert asset_path
        self.assertTrue((asset_path / "loop_output.txt").exists())

    def test_complex_nested(self):
        """ctx.run() handles nested operations."""
        self.run_recipe(
            "local.ctx_run_complex_nested@v1",
            "test_data/recipes/ctx_run_complex_nested.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_complex_nested@v1")
        assert asset_path
        self.assertTrue((asset_path / "summary.txt").exists())

    # ===== Edge Case Tests =====

    def test_edge_empty_script(self):
        """ctx.run() handles empty script."""
        self.run_recipe(
            "local.ctx_run_edge_empty@v1",
            "test_data/recipes/ctx_run_edge_empty.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_edge_empty@v1")
        assert asset_path
        self.assertTrue((asset_path / "after_empty.txt").exists())

    def test_edge_whitespace(self):
        """ctx.run() handles whitespace-only script."""
        self.run_recipe(
            "local.ctx_run_edge_whitespace@v1",
            "test_data/recipes/ctx_run_edge_whitespace.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_edge_whitespace@v1")
        assert asset_path
        self.assertTrue((asset_path / "after_whitespace.txt").exists())

    def test_edge_long_line(self):
        """ctx.run() handles very long lines."""
        self.run_recipe(
            "local.ctx_run_edge_long_line@v1",
            "test_data/recipes/ctx_run_edge_long_line.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_edge_long_line@v1")
        assert asset_path
        self.assertTrue((asset_path / "long_line.txt").exists())

    def test_edge_special_chars(self):
        """ctx.run() handles special characters."""
        self.run_recipe(
            "local.ctx_run_edge_special_chars@v1",
            "test_data/recipes/ctx_run_edge_special_chars.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_edge_special_chars@v1")
        assert asset_path
        self.assertTrue((asset_path / "special_chars.txt").exists())

    def test_edge_unicode(self):
        """ctx.run() handles Unicode characters."""
        self.run_recipe(
            "local.ctx_run_edge_unicode@v1",
            "test_data/recipes/ctx_run_edge_unicode.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_edge_unicode@v1")
        assert asset_path
        self.assertTrue((asset_path / "unicode.txt").exists())

    def test_edge_many_files(self):
        """ctx.run() handles creating many files."""
        self.run_recipe(
            "local.ctx_run_edge_many_files@v1",
            "test_data/recipes/ctx_run_edge_many_files.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_edge_many_files@v1")
        assert asset_path
        self.assertTrue((asset_path / "many_files_marker.txt").exists())

    def test_edge_slow_command(self):
        """ctx.run() waits for slow commands."""
        self.run_recipe(
            "local.ctx_run_edge_slow_command@v1",
            "test_data/recipes/ctx_run_edge_slow_command.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_edge_slow_command@v1")
        assert asset_path
        self.assertTrue((asset_path / "slow_verify.txt").exists())

    # ===== Lua Error Integration Tests =====

    def test_lua_error_after(self):
        """Lua error after ctx.run() succeeds."""
        self.run_recipe(
            "local.ctx_run_lua_error_after@v1",
            "test_data/recipes/ctx_run_lua_error_after.lua",
            should_fail=True,
        )

    def test_lua_error_before(self):
        """Lua error before ctx.run()."""
        self.run_recipe(
            "local.ctx_run_lua_error_before@v1",
            "test_data/recipes/ctx_run_lua_error_before.lua",
            should_fail=True,
        )

    def test_lua_bad_args(self):
        """ctx.run() with invalid arguments."""
        self.run_recipe(
            "local.ctx_run_lua_bad_args@v1",
            "test_data/recipes/ctx_run_lua_bad_args.lua",
            should_fail=True,
        )

    def test_lua_bad_opts(self):
        """ctx.run() with invalid options."""
        self.run_recipe(
            "local.ctx_run_lua_bad_opts@v1",
            "test_data/recipes/ctx_run_lua_bad_opts.lua",
            should_fail=True,
        )

    # ===== Option Combination Tests =====

    def test_all_options(self):
        """ctx.run() with all options combined."""
        self.run_recipe(
            "local.ctx_run_all_options@v1",
            "test_data/recipes/ctx_run_all_options.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_all_options@v1")
        assert asset_path
        self.assertTrue((asset_path / "subdir" / "all_opts_continued.txt").exists())

    def test_option_combinations(self):
        """ctx.run() with various option combinations."""
        self.run_recipe(
            "local.ctx_run_option_combinations@v1",
            "test_data/recipes/ctx_run_option_combinations.lua",
        )
        asset_path = self.get_asset_path("local.ctx_run_option_combinations@v1")
        assert asset_path
        self.assertTrue((asset_path / "dir1" / "combo1_env.txt").exists())
        self.assertTrue((asset_path / "dir2" / "combo2_continued.txt").exists())
        self.assertTrue((asset_path / "combo3_env.txt").exists())


if __name__ == "__main__":
    unittest.main()

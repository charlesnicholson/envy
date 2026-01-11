"""Functional tests for basic envy.run() functionality.

Tests basic execution, multiple commands, file operations, error handling,
CWD options, and shell-specific behavior.
"""

import os
import sys
import unittest
from pathlib import Path

from .test_ctx_run_base import CtxRunTestBase


class TestCtxRunBasic(CtxRunTestBase):
    """Tests for basic envy.run() functionality."""

    def test_basic_execution(self):
        """envy.run() executes shell commands successfully."""
        # Basic shell command execution, creates marker file
        spec = """-- Test basic envy.run() execution
IDENTITY = "local.ctx_run_basic@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path run_marker.txt -Value "Hello from ctx.run"
      Add-Content -Path run_marker.txt -Value ("Stage directory: " + (Get-Location).Path)
      exit 0
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "Hello from ctx.run" > run_marker.txt
      echo "Stage directory: $(pwd)" >> run_marker.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_basic.lua", spec)
        self.run_spec("local.ctx_run_basic@v1", "ctx_run_basic.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_basic@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "run_marker.txt").exists())

    def test_multiple_commands(self):
        """envy.run() executes multiple commands in sequence."""
        # Multiple commands in single run call
        spec = """-- Test envy.run() with multiple commands
IDENTITY = "local.ctx_run_multiple_cmds@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path cmd1.txt -Value "Command 1"
      Set-Content -Path cmd2.txt -Value "Command 2"
      Set-Content -Path cmd3.txt -Value "Command 3"
      Get-Content cmd1.txt, cmd2.txt, cmd3.txt | Set-Content -Path all_cmds.txt
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "Command 1" > cmd1.txt
      echo "Command 2" > cmd2.txt
      echo "Command 3" > cmd3.txt
      cat cmd1.txt cmd2.txt cmd3.txt > all_cmds.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_multiple_cmds.lua", spec)
        self.run_spec("local.ctx_run_multiple_cmds@v1", "ctx_run_multiple_cmds.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_multiple_cmds@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "all_cmds.txt").exists())

    def test_file_operations(self):
        """envy.run() can perform file operations."""
        # File copy/move operations via shell
        spec = """-- Test envy.run() with file operations
IDENTITY = "local.ctx_run_file_ops@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path original.txt -Value "original content"
      Copy-Item original.txt copy.txt -Force
      Move-Item copy.txt moved.txt -Force
      if (Test-Path moved.txt) {{
        Set-Content -Path ops_result.txt -Value "File operations successful"
      }} else {{
        exit 1
      }}
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "original content" > original.txt
      cp original.txt copy.txt
      mv copy.txt moved.txt
      test -f moved.txt && echo "File operations successful" > ops_result.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_file_ops.lua", spec)
        self.run_spec("local.ctx_run_file_ops@v1", "ctx_run_file_ops.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_file_ops@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "ops_result.txt").exists())

    def test_runs_in_stage_dir(self):
        """envy.run() executes in stage directory by default."""
        # Verify default cwd is stage_dir
        spec = """-- Test envy.run() executes in stage_dir by default
IDENTITY = "local.ctx_run_in_stage_dir@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path pwd_default.txt -Value (Get-Location).Path
      Get-ChildItem | Select-Object -ExpandProperty Name | Set-Content -Path ls_output.txt
      if (Test-Path file1.txt) {{
        Set-Content -Path stage_verification.txt -Value "Found file1.txt from archive"
      }}
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      pwd > pwd_default.txt
      ls > ls_output.txt
      test -f file1.txt && echo "Found file1.txt from archive" > stage_verification.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_in_stage_dir.lua", spec)
        self.run_spec("local.ctx_run_in_stage_dir@v1", "ctx_run_in_stage_dir.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_in_stage_dir@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "stage_verification.txt").exists())

    def test_with_pipes(self):
        """envy.run() supports shell pipes and redirection."""
        # Pipe and redirection support
        spec = """-- Test envy.run() with shell pipes and redirection
IDENTITY = "local.ctx_run_with_pipes@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
$content = @("line3", "line1", "line2")
$content | Sort-Object | Set-Content -Path sorted.txt
Get-Content sorted.txt | Where-Object {{ $_ -match "line2" }} | Set-Content -Path grepped.txt
Add-Content -Path grepped.txt -Value "Pipes work"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo -e "line3\\nline1\\nline2" | sort > sorted.txt
      cat sorted.txt | grep "line2" > grepped.txt
      echo "Pipes work" >> grepped.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_with_pipes.lua", spec)
        self.run_spec("local.ctx_run_with_pipes@v1", "ctx_run_with_pipes.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_with_pipes@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "grepped.txt").exists())


class TestCtxRunErrors(CtxRunTestBase):
    """Tests for envy.run() error handling."""

    def test_error_nonzero_exit(self):
        """envy.run() fails on non-zero exit code."""
        # Non-zero exit triggers failure
        spec = """-- Test envy.run() error on non-zero exit code
IDENTITY = "local.ctx_run_error_nonzero@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Write-Output "About to fail"
      Set-Content -Path will_fail.txt -Value "Intentional failure sentinel"
      exit 42
    ]], {{ shell = ENVY_SHELL.POWERSHELL, check = true }})
  else
    envy.run([[
      echo "About to fail"
      exit 42
    ]], {{ check = true }})
  end
end
"""
        self.write_spec("ctx_run_error_nonzero.lua", spec)
        self.run_spec(
            "local.ctx_run_error_nonzero@v1",
            "ctx_run_error_nonzero.lua",
            should_fail=True,
        )

    def test_invalid_cwd(self):
        """envy.run() fails when cwd doesn't exist."""
        # Invalid cwd path triggers error
        spec = """-- Test envy.run() error when cwd doesn't exist
IDENTITY = "local.ctx_run_invalid_cwd@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  local invalid_cwd
  local script
  if envy.PLATFORM == "windows" then
    invalid_cwd = "Z:/nonexistent/directory/that/does/not/exist"
    script = [[
      Write-Output "Should not execute"
    ]]
    envy.run(script, {{cwd = invalid_cwd, shell = ENVY_SHELL.POWERSHELL, check = true}})
  else
    invalid_cwd = "/nonexistent/directory/that/does/not/exist"
    envy.run([[
      echo "Should not execute"
    ]], {{cwd = invalid_cwd, check = true}})
  end
end
"""
        self.write_spec("ctx_run_invalid_cwd.lua", spec)
        self.run_spec(
            "local.ctx_run_invalid_cwd@v1",
            "ctx_run_invalid_cwd.lua",
            should_fail=True,
        )

    def test_command_not_found(self):
        """envy.run() fails when command doesn't exist."""
        # Non-existent command triggers error
        spec = """-- Test envy.run() error when command not found
IDENTITY = "local.ctx_run_command_not_found@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      cmd /c nonexistent_command_xyz123
      exit $LASTEXITCODE
    ]], {{ shell = ENVY_SHELL.POWERSHELL, check = true }})
  else
    envy.run([[
      nonexistent_command_xyz123
    ]], {{ check = true }})
  end
end
"""
        self.write_spec("ctx_run_command_not_found.lua", spec)
        self.run_spec(
            "local.ctx_run_command_not_found@v1",
            "ctx_run_command_not_found.lua",
            should_fail=True,
        )

    @unittest.skipIf(sys.platform == "win32", "Signals not supported on Windows")
    def test_signal_termination(self):
        """envy.run() reports signal termination."""
        # Signal termination handling
        spec = """-- Test envy.run() error on signal termination
IDENTITY = "local.ctx_run_signal_term@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Stop-Process -Id $PID -Force
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      kill -TERM $$
    ]])
  end
end
"""
        self.write_spec("ctx_run_signal_term.lua", spec)
        self.run_spec(
            "local.ctx_run_signal_term@v1",
            "ctx_run_signal_term.lua",
            should_fail=True,
        )

    @unittest.skipUnless(os.name != "nt", "requires POSIX shells")
    def test_shell_sh(self):
        """envy.run() executes explicitly via /bin/sh."""
        # Explicit sh shell specification
        spec = """-- Verify envy.run() with shell="sh" on POSIX hosts
IDENTITY = "local.ctx_run_shell_sh@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  if envy.PLATFORM == "windows" then
    error("ctx_run_shell_sh should not run on Windows")
  end

  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  envy.run([[\\
    set -eu\\
    printf "shell=sh\\n" > shell_sh_marker.txt\\
  ]], {{ shell = ENVY_SHELL.SH }})
end
"""
        self.write_spec("ctx_run_shell_sh.lua", spec)
        self.run_spec("local.ctx_run_shell_sh@v1", "ctx_run_shell_sh.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_shell_sh@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "shell_sh_marker.txt").exists())

    @unittest.skipUnless(os.name == "nt", "requires Windows CMD")
    def test_shell_cmd(self):
        """envy.run() executes explicitly via cmd.exe."""
        # Explicit CMD shell on Windows
        spec = """-- Verify envy.run() with shell="cmd" on Windows hosts
IDENTITY = "local.ctx_run_shell_cmd@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  if envy.PLATFORM ~= "windows" then
    error("ctx_run_shell_cmd should only run on Windows")
  end

  -- Use clean multi-line script without stray backslashes; ensure proper file creation.
  envy.run([[
@echo off
setlocal enabledelayedexpansion
echo shell=cmd>shell_cmd_marker.txt
  ]], {{ shell = ENVY_SHELL.CMD }})
end
"""
        self.write_spec("ctx_run_shell_cmd.lua", spec)
        self.run_spec("local.ctx_run_shell_cmd@v1", "ctx_run_shell_cmd.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_shell_cmd@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "shell_cmd_marker.txt").exists())


class TestCtxRunCwd(CtxRunTestBase):
    """Tests for envy.run() cwd option."""

    def test_cwd_relative(self):
        """envy.run() supports relative cwd paths."""
        # Relative cwd path handling
        spec = """-- Test envy.run() with relative cwd option
IDENTITY = "local.ctx_run_cwd_relative@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[New-Item -ItemType Directory -Force -Path "custom/subdir" | Out-Null]], {{ shell = ENVY_SHELL.POWERSHELL }})
    envy.run([[
      Set-Content -Path pwd_output.txt -Value (Get-Location).Path
      Set-Content -Path marker.txt -Value "Running in subdir"
    ]], {{cwd = "custom/subdir", shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[mkdir -p custom/subdir]])
    envy.run([[
      pwd > pwd_output.txt
      echo "Running in subdir" > marker.txt
    ]], {{cwd = "custom/subdir"}})
  end
end
"""
        self.write_spec("ctx_run_cwd_relative.lua", spec)
        self.run_spec("local.ctx_run_cwd_relative@v1", "ctx_run_cwd_relative.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_cwd_relative@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "custom" / "subdir" / "marker.txt").exists())

    def test_cwd_absolute(self):
        """envy.run() supports absolute cwd paths."""
        # Absolute cwd path handling
        spec = """-- Test envy.run() with absolute cwd path
IDENTITY = "local.ctx_run_cwd_absolute@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    local temp = os.getenv("TEMP") or "C:\\\\Temp"
    local needs_sep = temp:match("[/\\\\]$") == nil
    local target = temp .. (needs_sep and "\\\\" or "") .. "envy_ctx_run_test.txt"
    envy.run(string.format([[
      Set-Content -Path pwd_absolute.txt -Value (Get-Location).Path
      # Use Out-File with -Force to ensure file is created and flushed
      "Running in TEMP" | Out-File -FilePath "%s" -Force -Encoding ascii
      # Verify file was created
      if (-Not (Test-Path "%s")) {{
        throw "Failed to create test file"
      }}
    ]], target, target), {{cwd = temp, shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      pwd > pwd_absolute.txt
      echo "Running in /tmp" > /tmp/envy_ctx_run_test.txt
      # Verify file was created
      test -f /tmp/envy_ctx_run_test.txt || exit 1
    ]], {{cwd = "/tmp"}})
  end
end
"""
        self.write_spec("ctx_run_cwd_absolute.lua", spec)
        self.run_spec("local.ctx_run_cwd_absolute@v1", "ctx_run_cwd_absolute.lua")
        if os.name == "nt":
            target = Path(os.environ.get("TEMP", "C:/Temp")) / "envy_ctx_run_test.txt"
        else:
            target = Path("/tmp/envy_ctx_run_test.txt")
        self.assertTrue(target.exists())
        target.unlink(missing_ok=True)

    def test_cwd_parent(self):
        """envy.run() handles parent directory (..) in cwd."""
        # Parent dir (..) in cwd path
        spec = """-- Test envy.run() with parent directory (..) in cwd
IDENTITY = "local.ctx_run_cwd_parent@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      New-Item -ItemType Directory -Force -Path "deep/nested/dir" | Out-Null
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})

    envy.run([[
      Set-Content -Path pwd_from_parent.txt -Value (Get-Location).Path
      Set-Content -Path parent_marker.txt -Value "Using parent dir"
    ]], {{cwd = "deep/nested/..", shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      mkdir -p deep/nested/dir
    ]])

    envy.run([[
      pwd > pwd_from_parent.txt
      echo "Using parent dir" > parent_marker.txt
    ]], {{cwd = "deep/nested/.."}})
  end
end
"""
        self.write_spec("ctx_run_cwd_parent.lua", spec)
        self.run_spec("local.ctx_run_cwd_parent@v1", "ctx_run_cwd_parent.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_cwd_parent@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "deep" / "parent_marker.txt").exists())

    def test_cwd_nested(self):
        """envy.run() handles deeply nested relative cwd."""
        # Deeply nested relative cwd path
        spec = """-- Test envy.run() with deeply nested relative cwd
IDENTITY = "local.ctx_run_cwd_nested@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      New-Item -ItemType Directory -Force -Path "level1/level2/level3/level4" | Out-Null
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})

    envy.run([[
      Set-Content -Path pwd_nested.txt -Value (Get-Location).Path
      Set-Content -Path nested_marker.txt -Value "Deep nesting works"
    ]], {{cwd = "level1/level2/level3/level4", shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      mkdir -p level1/level2/level3/level4
    ]])

    envy.run([[
      pwd > pwd_nested.txt
      echo "Deep nesting works" > nested_marker.txt
    ]], {{cwd = "level1/level2/level3/level4"}})
  end
end
"""
        self.write_spec("ctx_run_cwd_nested.lua", spec)
        self.run_spec("local.ctx_run_cwd_nested@v1", "ctx_run_cwd_nested.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_cwd_nested@v1")
        assert pkg_path
        self.assertTrue(
            (
                pkg_path
                / "level1"
                / "level2"
                / "level3"
                / "level4"
                / "nested_marker.txt"
            ).exists()
        )


if __name__ == "__main__":
    unittest.main()

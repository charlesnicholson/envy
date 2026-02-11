"""Functional tests for envy.run() options.

Tests check mode, environment variables, and option combinations.
"""

import unittest

from .test_ctx_run_base import CtxRunTestBase


class TestCtxRunCheckMode(CtxRunTestBase):
    """Tests for envy.run() check mode options."""

    def test_default_check_throws_on_failure(self):
        """envy.run() throws on non-zero exit by default (no explicit check)."""
        spec = """-- Test envy.run() default check=true throws on failure
IDENTITY = "local.ctx_run_default_check@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      exit 7
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      exit 7
    ]])
  end
end
"""
        self.write_spec("ctx_run_default_check.lua", spec)
        self.run_spec(
            "local.ctx_run_default_check@v1",
            "ctx_run_default_check.lua",
            should_fail=True,
        )

    def test_check_mode_catches_failures(self):
        """envy.run() check mode catches command failures."""
        # check=true with set -euo pipefail
        spec = """-- Test envy.run() check mode catches failures
IDENTITY = "local.ctx_run_check_mode@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      cmd /c exit /b 7
      if ($LASTEXITCODE -ne 0) {{ exit $LASTEXITCODE }}
      Write-Output "This should not execute"
    ]], {{ shell = ENVY_SHELL.POWERSHELL, check = true }})
  else
    envy.run([[
      set -euo pipefail
      false
      echo "This should not execute"
    ]], {{ check = true }})
  end
end
"""
        self.write_spec("ctx_run_check_mode.lua", spec)
        self.run_spec(
            "local.ctx_run_check_mode@v1",
            "ctx_run_check_mode.lua",
            should_fail=True,
        )

    def test_continue_after_failure(self):
        """envy.run() with check=false continues execution after a failing command."""
        # check=false allows script to continue past failures
        spec = """-- Test envy.run() continues after a failing command
IDENTITY = "local.ctx_run_continue_after_failure@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  -- Script should keep running even if an intermediate command fails
  if envy.PLATFORM == "windows" then
    envy.run([[
      cmd /c exit 1
      Set-Content -Path continued.txt -Value "This executes even after false"
    ]], {{shell = ENVY_SHELL.POWERSHELL, check = false}})
  else
    envy.run([[
      false || true
      echo "This executes even after false" > continued.txt
    ]], {{check = false}})
  end
end
"""
        self.write_spec("ctx_run_continue_after_failure.lua", spec)
        self.run_spec(
            "local.ctx_run_continue_after_failure@v1",
            "ctx_run_continue_after_failure.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_continue_after_failure@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "continued.txt").exists())

    def test_check_undefined_variable(self):
        """envy.run() check mode catches undefined variables."""
        # set -u catches undefined variable
        spec = """-- Test envy.run() check mode catches undefined variables
IDENTITY = "local.ctx_run_check_undefined@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      $ErrorActionPreference = "Stop"
      Write-Output "About to use undefined variable"
      if (-not $env:UNDEFINED_VARIABLE_XYZ) {{ throw "Undefined variable" }}
      Write-Output "Value: $env:UNDEFINED_VARIABLE_XYZ"
      Set-Content -Path should_not_exist.txt -Value "Should not reach here"
    ]], {{ shell = ENVY_SHELL.POWERSHELL, check = true }})
  else
    envy.run([[
      set -euo pipefail
      echo "About to use undefined variable"
      echo "Value: $UNDEFINED_VARIABLE_XYZ"
      echo "Should not reach here" > should_not_exist.txt
    ]], {{ check = true }})
  end
end
"""
        self.write_spec("ctx_run_check_undefined.lua", spec)
        self.run_spec(
            "local.ctx_run_check_undefined@v1",
            "ctx_run_check_undefined.lua",
            should_fail=True,
        )

    def test_check_pipefail(self):
        """envy.run() check mode catches pipe failures."""
        # pipefail catches failure in pipe
        spec = """-- Test envy.run() check mode catches pipe failures
IDENTITY = "local.ctx_run_check_pipefail@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      cmd /c "echo Start | cmd /c exit /b 3"
      if ($LASTEXITCODE -ne 0) {{ exit $LASTEXITCODE }}
    ]], {{ shell = ENVY_SHELL.POWERSHELL, check = true }})
  else
    envy.run([[
      set -euo pipefail
      echo "Start" | false | cat > should_fail.txt
    ]], {{ check = true }})
  end
end
"""
        self.write_spec("ctx_run_check_pipefail.lua", spec)
        self.run_spec(
            "local.ctx_run_check_pipefail@v1",
            "ctx_run_check_pipefail.lua",
            should_fail=True,
        )

    def test_check_false_nonzero(self):
        """envy.run() with check=false allows non-zero exit codes."""
        # check=false returns exit code without error
        spec = """-- Test envy.run() with check=false allows non-zero exit codes
IDENTITY = "local.ctx_run_check_false_nonzero@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  local res
  if envy.PLATFORM == "windows" then
    res = envy.run([[
      Write-Output "Command that fails"
      exit 7
    ]], {{ shell = ENVY_SHELL.POWERSHELL, check = false }})
  else
    res = envy.run([[
      echo "Command that fails"
      exit 7
    ]], {{ check = false }})
  end

  if res.exit_code ~= 7 then
    error("Expected exit_code to be 7, got " .. tostring(res.exit_code))
  end

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path continued_after_failure.txt -Value "Spec continued after non-zero exit"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "Spec continued after non-zero exit" > continued_after_failure.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_check_false_nonzero.lua", spec)
        self.run_spec(
            "local.ctx_run_check_false_nonzero@v1",
            "ctx_run_check_false_nonzero.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_check_false_nonzero@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "continued_after_failure.txt").exists())

    def test_check_false_capture(self):
        """envy.run() with check=false and capture returns exit_code and output."""
        # check=false + capture=true returns structured result
        spec = """-- Test envy.run() with check=false and capture returns exit_code and output
IDENTITY = "local.ctx_run_check_false_capture@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  local res
  if envy.PLATFORM == "windows" then
    res = envy.run([[
      Write-Output "stdout content"
      [Console]::Error.WriteLine("stderr content")
      exit 42
    ]], {{ shell = ENVY_SHELL.POWERSHELL, check = false, capture = true }})
  else
    res = envy.run([[
      echo "stdout content"
      echo "stderr content" >&2
      exit 42
    ]], {{ check = false, capture = true }})
  end

  if res.exit_code ~= 42 then
    error("Expected exit_code to be 42, got " .. tostring(res.exit_code))
  end

  if not res.stdout:match("stdout content") then
    error("Expected stdout to contain 'stdout content', got: " .. tostring(res.stdout))
  end

  if not res.stderr:match("stderr content") then
    error("Expected stderr to contain 'stderr content', got: " .. tostring(res.stderr))
  end

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path capture_success.txt -Value "Captured output successfully"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "Captured output successfully" > capture_success.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_check_false_capture.lua", spec)
        self.run_spec(
            "local.ctx_run_check_false_capture@v1",
            "ctx_run_check_false_capture.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_check_false_capture@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "capture_success.txt").exists())


class TestCtxRunEnvironment(CtxRunTestBase):
    """Tests for envy.run() environment variable handling."""

    def test_env_custom(self):
        """envy.run() supports custom environment variables."""
        # Custom env vars passed to command
        spec = """-- Test envy.run() with custom environment variables
IDENTITY = "local.ctx_run_env_custom@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  local env_values = {{MY_VAR = "test_value", MY_NUM = "42"}}

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path env_output.txt -Value ("MY_VAR=" + $env:MY_VAR)
      Add-Content -Path env_output.txt -Value ("MY_NUM=" + $env:MY_NUM)
      if ($env:PATH) {{ $status = "yes" }} else {{ $status = "" }}
      Add-Content -Path env_output.txt -Value ("PATH_AVAILABLE=" + $status)
    ]], {{env = env_values, shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      echo "MY_VAR=$MY_VAR" > env_output.txt
      echo "MY_NUM=$MY_NUM" >> env_output.txt
      echo "PATH_AVAILABLE=${{PATH:+yes}}" >> env_output.txt
    ]], {{env = env_values}})
  end
end
"""
        self.write_spec("ctx_run_env_custom.lua", spec)
        self.run_spec("local.ctx_run_env_custom@v1", "ctx_run_env_custom.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_env_custom@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "env_output.txt").exists())
        content = (pkg_path / "env_output.txt").read_text()
        self.assertIn("MY_VAR=test_value", content)
        self.assertIn("MY_NUM=42", content)

    def test_env_inherit(self):
        """envy.run() inherits environment variables like PATH."""
        # PATH inherited from parent environment
        spec = """-- Test envy.run() inherits environment variables like PATH
IDENTITY = "local.ctx_run_env_inherit@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path inherited_path.txt -Value ("PATH=" + $env:Path)
      $echoPath = (Get-Command echo.exe).Source
      Set-Content -Path which_echo.txt -Value $echoPath
      if ($env:Path -and $env:Path.Length -gt 0) {{
        Set-Content -Path path_verification.txt -Value "PATH inherited"
      }} else {{
        exit 1
      }}
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "PATH=$PATH" > inherited_path.txt
      which echo > which_echo.txt
      test -n "$PATH" && echo "PATH inherited" > path_verification.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_env_inherit.lua", spec)
        self.run_spec("local.ctx_run_env_inherit@v1", "ctx_run_env_inherit.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_env_inherit@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "path_verification.txt").exists())

    def test_env_override(self):
        """envy.run() can override inherited environment variables."""
        # Override inherited env var
        spec = """-- Test envy.run() can override inherited environment variables
IDENTITY = "local.ctx_run_env_override@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  -- Override a variable (USER is typically set)
    if envy.PLATFORM == "windows" then
      envy.run([[
        if ($env:USER -ne 'test_override_user') {{ exit 42 }}
        Set-Content -Path overridden_user.txt -Value ("USER=" + $env:USER)
      ]], {{env = {{USER = "test_override_user"}}, shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      echo "USER=$USER" > overridden_user.txt
    ]], {{env = {{USER = "test_override_user"}}}})
  end
end
"""
        self.write_spec("ctx_run_env_override.lua", spec)
        self.run_spec("local.ctx_run_env_override@v1", "ctx_run_env_override.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_env_override@v1")
        assert pkg_path
        content = (pkg_path / "overridden_user.txt").read_text()
        self.assertIn("USER=test_override_user", content)

    def test_env_empty(self):
        """envy.run() with empty env table still inherits."""
        # Empty env table preserves inherited env
        spec = """-- Test envy.run() with empty env table still inherits
IDENTITY = "local.ctx_run_env_empty@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path empty_env.txt -Value "Empty env table works"
      if ($env:PATH) {{
        Add-Content -Path empty_env.txt -Value "PATH still available"
      }}
    ]], {{env = {{}}, shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      echo "Empty env table works" > empty_env.txt
      test -n "$PATH" && echo "PATH still available" >> empty_env.txt
    ]], {{env = {{}}}})
  end
end
"""
        self.write_spec("ctx_run_env_empty.lua", spec)
        self.run_spec("local.ctx_run_env_empty@v1", "ctx_run_env_empty.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_env_empty@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "empty_env.txt").exists())

    def test_env_complex(self):
        """envy.run() handles complex environment values."""
        # Complex env values with special chars
        spec = """-- Test envy.run() with complex environment variables
IDENTITY = "local.ctx_run_env_complex@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  local env_values = {{
    STRING = "hello_world",
    NUMBER = "12345",
    WITH_SPACE = "value with spaces",
    SPECIAL = "a=b:c;d"
  }}

    if envy.PLATFORM == "windows" then
      envy.run([[
        if (-not $env:STRING) {{ exit 44 }}
        Set-Content -Path env_complex.txt -Value ("STRING=" + $env:STRING)
        Add-Content -Path env_complex.txt -Value ("NUMBER=" + $env:NUMBER)
        Add-Content -Path env_complex.txt -Value ("WITH_SPACE=" + $env:WITH_SPACE)
        Add-Content -Path env_complex.txt -Value ("SPECIAL=" + $env:SPECIAL)
      ]], {{env = env_values, shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      echo "STRING=$STRING" > env_complex.txt
      echo "NUMBER=$NUMBER" >> env_complex.txt
      echo "WITH_SPACE=$WITH_SPACE" >> env_complex.txt
      echo "SPECIAL=$SPECIAL" >> env_complex.txt
    ]], {{env = env_values}})
  end
end
"""
        self.write_spec("ctx_run_env_complex.lua", spec)
        self.run_spec("local.ctx_run_env_complex@v1", "ctx_run_env_complex.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_env_complex@v1")
        assert pkg_path
        content = (pkg_path / "env_complex.txt").read_text()
        self.assertIn("STRING=hello_world", content)
        self.assertIn("NUMBER=12345", content)
        self.assertIn("WITH_SPACE=value with spaces", content)


class TestCtxRunOptionCombinations(CtxRunTestBase):
    """Tests for envy.run() option combinations."""

    def test_all_options(self):
        """envy.run() with all options combined."""
        # cwd + env + failure continuation
        spec = """-- Test envy.run() with all options combined
IDENTITY = "local.ctx_run_all_options@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[New-Item -ItemType Directory -Force -Path subdir | Out-Null]], {{ shell = ENVY_SHELL.POWERSHELL }})
    envy.run([[
      Set-Content -Path all_opts_pwd.txt -Value (Get-Location).Path
      Set-Content -Path all_opts_env.txt -Value ("MY_VAR=" + $env:MY_VAR)
      Add-Content -Path all_opts_env.txt -Value ("ANOTHER=" + $env:ANOTHER)
      cmd /c exit 1
      Set-Content -Path all_opts_continued.txt -Value "Continued after false"
    ]], {{cwd = "subdir", env = {{MY_VAR = "test", ANOTHER = "value"}}, shell = ENVY_SHELL.POWERSHELL, check = false}})
  else
    envy.run([[mkdir -p subdir]])
    envy.run([[
      pwd > all_opts_pwd.txt
      echo "MY_VAR=$MY_VAR" > all_opts_env.txt
      echo "ANOTHER=$ANOTHER" >> all_opts_env.txt
      false || true
      echo "Continued after false" > all_opts_continued.txt
    ]], {{cwd = "subdir", env = {{MY_VAR = "test", ANOTHER = "value"}}, check = false}})
  end
end
"""
        self.write_spec("ctx_run_all_options.lua", spec)
        self.run_spec("local.ctx_run_all_options@v1", "ctx_run_all_options.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_all_options@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "subdir" / "all_opts_pwd.txt").exists())
        self.assertTrue((pkg_path / "subdir" / "all_opts_env.txt").exists())
        self.assertTrue((pkg_path / "subdir" / "all_opts_continued.txt").exists())

    def test_option_combinations(self):
        """envy.run() with various option combinations."""
        # Multiple option combos in one spec
        spec = """-- Test envy.run() with various option combinations
IDENTITY = "local.ctx_run_option_combinations@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      New-Item -ItemType Directory -Force -Path dir1,dir2 | Out-Null
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      mkdir -p dir1 dir2
    ]])
  end

  -- Combination 1: cwd + env
  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path combo1_pwd.txt -Value (Get-Location).Path
      Set-Content -Path combo1_env.txt -Value ("VAR1=" + $env:VAR1)
    ]], {{cwd = "dir1", env = {{VAR1 = "value1"}}, shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      pwd > combo1_pwd.txt
      echo "VAR1=$VAR1" > combo1_env.txt
    ]], {{cwd = "dir1", env = {{VAR1 = "value1"}}}})
  end

  -- Combination 2: cwd with a failing command in the middle
  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path combo2_pwd.txt -Value (Get-Location).Path
      cmd /c exit 1
      Set-Content -Path combo2_continued.txt -Value "After false"
    ]], {{cwd = "dir2", shell = ENVY_SHELL.POWERSHELL, check = false}})
  else
    envy.run([[
      pwd > combo2_pwd.txt
      false || true
      echo "After false" > combo2_continued.txt
    ]], {{cwd = "dir2", check = false}})
  end

  -- Combination 3: env with a failing command (default cwd)
  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path combo3_env.txt -Value ("VAR2=" + $env:VAR2)
      cmd /c exit 1
      Add-Content -Path combo3_env.txt -Value "Continued"
    ]], {{env = {{VAR2 = "value2"}}, shell = ENVY_SHELL.POWERSHELL, check = false}})
  else
    envy.run([[
      echo "VAR2=$VAR2" > combo3_env.txt
      false || true
      echo "Continued" >> combo3_env.txt
    ]], {{env = {{VAR2 = "value2"}}, check = false}})
  end

  -- Combination 4: Just env
  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path combo4_env.txt -Value ("VAR3=" + $env:VAR3)
    ]], {{env = {{VAR3 = "value3"}}, shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      echo "VAR3=$VAR3" > combo4_env.txt
    ]], {{env = {{VAR3 = "value3"}}}})
  end

  -- Combination 5: Just cwd
  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path combo5_pwd.txt -Value (Get-Location).Path
    ]], {{cwd = "dir1", shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      pwd > combo5_pwd.txt
    ]], {{cwd = "dir1"}})
  end

  -- Combination 6: Failing command without any other options
  if envy.PLATFORM == "windows" then
    envy.run([[
      cmd /c exit 1
      Set-Content -Path combo6_continued.txt -Value "Standalone failure scenario"
    ]], {{shell = ENVY_SHELL.POWERSHELL, check = false}})
  else
    envy.run([[
      false || true
      echo "Standalone failure scenario" > combo6_continued.txt
    ]], {{check = false}})
  end
end

"""
        self.write_spec("ctx_run_option_combinations.lua", spec)
        self.run_spec(
            "local.ctx_run_option_combinations@v1",
            "ctx_run_option_combinations.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_option_combinations@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "dir1" / "combo1_pwd.txt").exists())
        self.assertTrue((pkg_path / "dir1" / "combo1_env.txt").exists())
        self.assertTrue((pkg_path / "dir2" / "combo2_continued.txt").exists())
        self.assertTrue((pkg_path / "combo3_env.txt").exists())
        self.assertTrue((pkg_path / "combo4_env.txt").exists())
        self.assertTrue((pkg_path / "combo6_continued.txt").exists())


if __name__ == "__main__":
    unittest.main()

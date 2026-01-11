"""Functional tests for complex envy.run() scenarios.

Tests complex workflows, edge cases, Lua error handling, and output capture.
"""

import unittest

from .test_ctx_run_base import CtxRunTestBase


class TestCtxRunComplexScenarios(CtxRunTestBase):
    """Tests for complex envy.run() scenarios."""

    def test_complex_workflow(self):
        """envy.run() with complex real-world workflow."""
        spec = """-- Test envy.run() with complex real-world workflow
IDENTITY = "local.ctx_run_complex_workflow@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      @('build','src','include') | ForEach-Object {{ if (-not (Test-Path $_)) {{ New-Item -ItemType Directory -Path $_ | Out-Null }} }}
      Set-Content -Path config.mk -Value "PROJECT=myapp"
      Add-Content -Path config.mk -Value "VERSION=1.0.0"
      Set-Content -Path src/version.h -Value '#define VERSION "1.0.0"'
      Push-Location build
      Set-Content -Path config.log -Value "Configuring..."
      Set-Content -Path build.mk -Value "CFLAGS=-O2 -Wall"
      Pop-Location
      if (-not (Test-Path config.mk)) {{ exit 1 }}
      if (-not (Test-Path build/build.mk)) {{ exit 1 }}
      Set-Content -Path workflow_complete.txt -Value "Workflow complete"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      mkdir -p build src include
      echo "PROJECT=myapp" > config.mk
      echo "VERSION=1.0.0" >> config.mk
    ]])

    envy.run([[
      echo "#define VERSION \\"1.0.0\\"" > src/version.h
    ]])

    envy.run([[
      cd build
      echo "Configuring..." > config.log
      echo "CFLAGS=-O2 -Wall" > build.mk
    ]], {{cwd = "."}})

    envy.run([[
      test -f config.mk || exit 1
      test -d build || exit 1
      test -f build/build.mk || exit 1
      echo "Workflow complete" > workflow_complete.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_complex_workflow.lua", spec)
        self.run_spec("local.ctx_run_complex_workflow@v1", "ctx_run_complex_workflow.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_complex_workflow@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "workflow_complete.txt").exists())

    def test_complex_env_manip(self):
        """envy.run() with complex environment manipulation."""
        spec = """-- Test envy.run() with complex environment manipulation
IDENTITY = "local.ctx_run_complex_env_manip@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path env_step1.txt -Value ("BASE_VAR=" + $env:BASE_VAR)
    ]], {{env = {{BASE_VAR = "base_value"}}, shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      echo "BASE_VAR=$BASE_VAR" > env_step1.txt
    ]], {{env = {{BASE_VAR = "base_value"}}}})
  end

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path env_step2.txt -Value ("BASE_VAR=" + $env:BASE_VAR)
      Add-Content -Path env_step2.txt -Value ("EXTRA_VAR=" + $env:EXTRA_VAR)
      Add-Content -Path env_step2.txt -Value ("ANOTHER=" + $env:ANOTHER)
    ]], {{env = {{
      BASE_VAR = "modified",
      EXTRA_VAR = "extra",
      ANOTHER = "another"
    }}, shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      echo "BASE_VAR=$BASE_VAR" > env_step2.txt
      echo "EXTRA_VAR=$EXTRA_VAR" >> env_step2.txt
      echo "ANOTHER=$ANOTHER" >> env_step2.txt
    ]], {{env = {{
      BASE_VAR = "modified",
      EXTRA_VAR = "extra",
      ANOTHER = "another"
    }}}})
  end
end
"""
        self.write_spec("ctx_run_complex_env_manip.lua", spec)
        self.run_spec("local.ctx_run_complex_env_manip@v1", "ctx_run_complex_env_manip.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_complex_env_manip@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "env_step1.txt").exists())
        self.assertTrue((pkg_path / "env_step2.txt").exists())

    def test_complex_conditional(self):
        """envy.run() with conditional operations."""
        spec = """-- Test envy.run() with conditional operations
IDENTITY = "local.ctx_run_complex_conditional@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      $osInfo = @("Running on Windows", "Use Windows commands")
      $osInfo | Set-Content -Path os_info.txt

      if (Test-Path file1.txt) {{
        $lines = (Get-Content file1.txt | Measure-Object -Line).Lines
        Set-Content -Path test_info.txt -Value $lines
        Add-Content -Path test_info.txt -Value "File processed"
      }} else {{
        Set-Content -Path test_info.txt -Value "No file to process"
      }}

      if (Test-Path .) {{
        try {{
          Set-Content -Path dir_check.txt -Value "Directory is writable"
        }} catch {{
          Set-Content -Path dir_check.txt -Value "Directory is read-only"
        }}
      }}
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      if [ "$(uname)" = "Darwin" ]; then
        echo "Running on macOS" > os_info.txt
        echo "Use BSD commands" >> os_info.txt
      elif [ "$(uname)" = "Linux" ]; then
        echo "Running on Linux" > os_info.txt
        echo "Use GNU commands" >> os_info.txt
      else
        echo "Unknown OS" > os_info.txt
      fi

      if [ -f file1.txt ]; then
        wc -l file1.txt > test_info.txt
        echo "File processed" >> test_info.txt
      else
        echo "No file to process" > test_info.txt
      fi

      if [ -d . ]; then
        if [ -w . ]; then
          echo "Directory is writable" > dir_check.txt
        else
          echo "Directory is read-only" > dir_check.txt
        fi
      fi
    ]])
  end
end
"""
        self.write_spec("ctx_run_complex_conditional.lua", spec)
        self.run_spec("local.ctx_run_complex_conditional@v1", "ctx_run_complex_conditional.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_complex_conditional@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "os_info.txt").exists())

    def test_complex_loops(self):
        """envy.run() handles loops and iterations."""
        spec = """-- Test envy.run() with loops and iterations
IDENTITY = "local.ctx_run_complex_loops@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Remove-Item loop_output.txt, file_loop.txt, while_loop.txt, even_numbers.txt, odd_numbers.txt -ErrorAction SilentlyContinue
      for ($i = 1; $i -le 5; $i++) {{
        $line = "Iteration $i"
        if ($i -eq 1) {{
          Set-Content -Path loop_output.txt -Value $line
        }} else {{
          Add-Content -Path loop_output.txt -Value $line
        }}
      }}

      foreach ($f in Get-ChildItem -Filter *.txt) {{
        $line = "Processing $($f.Name)"
        if (Test-Path file_loop.txt) {{
          Add-Content -Path file_loop.txt -Value $line
        }} else {{
          Set-Content -Path file_loop.txt -Value $line
        }}
      }}

      $counter = 0
      while ($counter -lt 3) {{
        $line = "Counter: $counter"
        if ($counter -eq 0) {{
          Set-Content -Path while_loop.txt -Value $line
        }} else {{
          Add-Content -Path while_loop.txt -Value $line
        }}
        $counter++
      }}

      for ($num = 1; $num -le 10; $num++) {{
        if ($num % 2 -eq 0) {{
          if (Test-Path even_numbers.txt) {{
            Add-Content -Path even_numbers.txt -Value "$num is even"
          }} else {{
            Set-Content -Path even_numbers.txt -Value "$num is even"
          }}
        }} else {{
          if (Test-Path odd_numbers.txt) {{
            Add-Content -Path odd_numbers.txt -Value "$num is odd"
          }} else {{
            Set-Content -Path odd_numbers.txt -Value "$num is odd"
          }}
        }}
      }}
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      for i in {{1..5}}; do
        echo "Iteration $i" >> loop_output.txt
      done

      for f in *.txt; do
        if [ -f "$f" ]; then
          echo "Processing $f" >> file_loop.txt
        fi
      done

      counter=0
      while [ $counter -lt 3 ]; do
        echo "Counter: $counter" >> while_loop.txt
        counter=$((counter + 1))
      done

      for num in {{1..10}}; do
        if [ $((num % 2)) -eq 0 ]; then
          echo "$num is even" >> even_numbers.txt
        else
          echo "$num is odd" >> odd_numbers.txt
        fi
      done
    ]])
  end
end
"""
        self.write_spec("ctx_run_complex_loops.lua", spec)
        self.run_spec("local.ctx_run_complex_loops@v1", "ctx_run_complex_loops.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_complex_loops@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "loop_output.txt").exists())

    def test_complex_nested(self):
        """envy.run() handles nested operations."""
        spec = """-- Test envy.run() with nested operations and complex scripts
IDENTITY = "local.ctx_run_complex_nested@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      $level2 = @("level2a", "level2b")
      $level3 = @("level3a", "level3b")
      foreach ($l2 in $level2) {{
        foreach ($l3 in $level3) {{
          $path = Join-Path -Path "level1" -ChildPath (Join-Path $l2 $l3)
          New-Item -ItemType Directory -Force -Path $path | Out-Null
          Set-Content -Path (Join-Path $path "data.txt") -Value ("Content in " + $path)
        }}
      }}

      Get-ChildItem -Path level1 -Recurse -Filter data.txt | ForEach-Object {{
        Add-Content -Path found_files.txt -Value ("Found in " + $_.Directory.Name)
      }}

      $dirCount = ([System.IO.Directory]::GetDirectories("level1", "*", [System.IO.SearchOption]::AllDirectories).Count + 1)
      $fileCount = [System.IO.Directory]::GetFiles("level1", "*", [System.IO.SearchOption]::AllDirectories).Count
      Set-Content -Path summary.txt -Value ("Total directories: " + $dirCount)
      Add-Content -Path summary.txt -Value ("Total files: " + $fileCount)

      foreach ($dir in Get-ChildItem -Path level1 -Directory) {{
        $count = (Get-ChildItem -Path $dir.FullName -Recurse -File).Count
        if ($count -gt 0) {{
          Add-Content -Path dir_summary.txt -Value ("Directory " + $dir.FullName + " has " + $count + " files")
        }}
      }}
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      mkdir -p level1/{{level2a,level2b}}/{{level3a,level3b}}

      for l2 in level1/level2*; do
        for l3 in $l2/level3*; do
          echo "Content in $l3" > "$l3/data.txt"
        done
      done

      find level1 -type f -name "data.txt" | while read file; do
        dirname=$(dirname "$file")
        basename=$(basename "$dirname")
        echo "Found in $basename" >> found_files.txt
      done

      echo "Total directories: $(find level1 -type d | wc -l)" > summary.txt
      echo "Total files: $(find level1 -type f | wc -l)" >> summary.txt

      for dir in level1/level2*; do
        if [ -d "$dir" ]; then
          count=$(find "$dir" -type f | wc -l)
          if [ $count -gt 0 ]; then
            echo "Directory $dir has $count files" >> dir_summary.txt
          fi
        fi
      done
    ]])
  end
end
"""
        self.write_spec("ctx_run_complex_nested.lua", spec)
        self.run_spec("local.ctx_run_complex_nested@v1", "ctx_run_complex_nested.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_complex_nested@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "summary.txt").exists())


class TestCtxRunEdgeCases(CtxRunTestBase):
    """Tests for edge case handling in envy.run()."""

    def test_edge_empty(self):
        """envy.run() with empty script."""
        spec = """-- Test envy.run() with empty script
IDENTITY = "local.ctx_run_edge_empty@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[]], {{ shell = ENVY_SHELL.POWERSHELL }})
    envy.run([[
      Set-Content -Path after_empty.txt -Value "After empty script"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[]])
    envy.run([[
      echo "After empty script" > after_empty.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_edge_empty.lua", spec)
        self.run_spec("local.ctx_run_edge_empty@v1", "ctx_run_edge_empty.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_edge_empty@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "after_empty.txt").exists())

    def test_edge_whitespace(self):
        """envy.run() with only whitespace and comments."""
        spec = """-- Test envy.run() with only whitespace and comments
IDENTITY = "local.ctx_run_edge_whitespace@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      # This is a comment

      # Another comment


    ]], {{ shell = ENVY_SHELL.POWERSHELL }})

    envy.run([[
      Set-Content -Path after_whitespace.txt -Value "After whitespace script"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      # This is a comment

      # Another comment


    ]])

    envy.run([[
      echo "After whitespace script" > after_whitespace.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_edge_whitespace.lua", spec)
        self.run_spec("local.ctx_run_edge_whitespace@v1", "ctx_run_edge_whitespace.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_edge_whitespace@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "after_whitespace.txt").exists())

    def test_edge_long_line(self):
        """envy.run() handles very long lines."""
        spec = """-- Test envy.run() with very long lines
IDENTITY = "local.ctx_run_edge_long_line@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  local long = "This is a very long line with lots of content AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"

  if envy.PLATFORM == "windows" then
    envy.run(string.format([[
      Set-Content -Path long_line.txt -Value "%s"
    ]], long), {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run(string.format([[
      echo "%s" > long_line.txt
    ]], long))
  end
end
"""
        self.write_spec("ctx_run_edge_long_line.lua", spec)
        self.run_spec("local.ctx_run_edge_long_line@v1", "ctx_run_edge_long_line.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_edge_long_line@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "long_line.txt").exists())

    def test_edge_many_files(self):
        """envy.run() creating many files."""
        spec = """-- Test envy.run() creating many files
IDENTITY = "local.ctx_run_edge_many_files@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      New-Item -ItemType Directory -Force -Path many_files | Out-Null
      foreach ($i in 1..50) {{
        $path = "many_files/file_$i.txt"
        Set-Content -Path $path -Value ("File " + $i + " content")
      }}
      Set-Content -Path many_files_marker.txt -Value "Created many files"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      mkdir -p many_files
      for i in {{1..50}}; do
        echo "File $i content" > "many_files/file_$i.txt"
      done
      echo "Created many files" > many_files_marker.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_edge_many_files.lua", spec)
        self.run_spec("local.ctx_run_edge_many_files@v1", "ctx_run_edge_many_files.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_edge_many_files@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "many_files_marker.txt").exists())

    def test_edge_special_chars(self):
        """envy.run() with special characters."""
        spec = """-- Test envy.run() with special characters
IDENTITY = "local.ctx_run_edge_special_chars@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path special_chars.txt -Value 'Special chars: !@#$%^&*()_+-=[]{{}}|;:'',.<>?/~`'
      Add-Content -Path special_chars.txt -Value 'Quotes: "double" ''single' ''
      Add-Content -Path special_chars.txt -Value 'Backslash: \\ and newline: (literal)'
      if (-not (Test-Path special_chars.txt)) {{ exit 1 }}
      exit 0
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "Special chars: !@#$%^&*()_+-=[]{{}}|;:',.<>?/~\\`" > special_chars.txt
      echo "Quotes: \\"double\\" 'single'" >> special_chars.txt
      echo "Backslash: \\\\ and newline: (literal)" >> special_chars.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_edge_special_chars.lua", spec)
        self.run_spec("local.ctx_run_edge_special_chars@v1", "ctx_run_edge_special_chars.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_edge_special_chars@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "special_chars.txt").exists())

    def test_edge_unicode(self):
        """envy.run() with Unicode characters."""
        spec = """-- Test envy.run() with Unicode characters
IDENTITY = "local.ctx_run_edge_unicode@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path unicode.txt -Value "Unicode: Hello 世界 🌍 café"
      Add-Content -Path unicode.txt -Value "More Unicode: Ω α β γ δ"
      Add-Content -Path unicode.txt -Value "Emoji: 😀 🎉 🚀"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "Unicode: Hello 世界 🌍 café" > unicode.txt
      echo "More Unicode: Ω α β γ δ" >> unicode.txt
      echo "Emoji: 😀 🎉 🚀" >> unicode.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_edge_unicode.lua", spec)
        self.run_spec("local.ctx_run_edge_unicode@v1", "ctx_run_edge_unicode.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_edge_unicode@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "unicode.txt").exists())

    def test_edge_slow_command(self):
        """envy.run() with slow command (ensures we wait for completion)."""
        spec = """-- Test envy.run() with slow command (ensures we wait for completion)
IDENTITY = "local.ctx_run_edge_slow_command@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path slow_start.txt -Value "Starting slow command"
      Start-Sleep -Seconds 1
      Set-Content -Path slow_end.txt -Value "Finished slow command"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})

    envy.run([[
      if ((Test-Path slow_start.txt) -and (Test-Path slow_end.txt)) {{
        Set-Content -Path slow_verify.txt -Value "Both files exist"
      }} else {{
        exit 1
      }}
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "Starting slow command" > slow_start.txt
      sleep 1
      echo "Finished slow command" > slow_end.txt
    ]])

    envy.run([[
      test -f slow_start.txt && test -f slow_end.txt && echo "Both files exist" > slow_verify.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_edge_slow_command.lua", spec)
        self.run_spec("local.ctx_run_edge_slow_command@v1", "ctx_run_edge_slow_command.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_edge_slow_command@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "slow_verify.txt").exists())


class TestCtxRunLuaErrors(CtxRunTestBase):
    """Tests for Lua error handling with envy.run()."""

    def test_lua_error_after(self):
        """Lua error after envy.run() succeeds."""
        spec = """-- Test Lua error after envy.run() succeeds
IDENTITY = "local.ctx_run_lua_error_after@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path before_lua_error.txt -Value "Before Lua error"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "Before Lua error" > before_lua_error.txt
    ]])
  end

  -- Then Lua error
  error("Intentional Lua error after ctx.run")
end
"""
        self.write_spec("ctx_run_lua_error_after.lua", spec)
        self.run_spec("local.ctx_run_lua_error_after@v1", "ctx_run_lua_error_after.lua", should_fail=True)

    def test_lua_error_before(self):
        """Lua error before envy.run()."""
        spec = """-- Test Lua error before envy.run()
IDENTITY = "local.ctx_run_lua_error_before@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  -- Lua error happens first
  error("Intentional Lua error before ctx.run")

  -- This should never execute
  envy.run([[
    echo "After Lua error" > after_lua_error.txt
  ]])
end
"""
        self.write_spec("ctx_run_lua_error_before.lua", spec)
        self.run_spec("local.ctx_run_lua_error_before@v1", "ctx_run_lua_error_before.lua", should_fail=True)

    def test_lua_bad_args(self):
        """envy.run() with invalid arguments (Lua error)."""
        spec = """-- Test envy.run() with invalid arguments (Lua error)
IDENTITY = "local.ctx_run_lua_bad_args@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  -- Call with invalid argument value (nil) to ensure failure consistently.
  envy.run(nil)
end
"""
        self.write_spec("ctx_run_lua_bad_args.lua", spec)
        self.run_spec("local.ctx_run_lua_bad_args@v1", "ctx_run_lua_bad_args.lua", should_fail=True)

    def test_lua_bad_options(self):
        """envy.run() with invalid options (Lua error)."""
        spec = """-- Test envy.run() with invalid options (Lua error)
IDENTITY = "local.ctx_run_lua_bad_opts@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  -- Call with non-table options (should cause Lua error)
  envy.run([[
    echo "test"
  ]], "not a table")
end
"""
        self.write_spec("ctx_run_lua_bad_options.lua", spec)
        self.run_spec("local.ctx_run_lua_bad_opts@v1", "ctx_run_lua_bad_options.lua", should_fail=True)


class TestCtxRunOutput(CtxRunTestBase):
    """Tests for envy.run() output handling."""

    def test_output_stdout(self):
        """envy.run() captures stdout output."""
        spec = """-- Test envy.run() captures stdout output
IDENTITY = "local.ctx_run_output_stdout@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Write-Output "Line 1 to stdout"
      Write-Output "Line 2 to stdout"
      Write-Output "Line 3 to stdout"
      "stdout test complete" | Out-File -Encoding UTF8 stdout_marker.txt
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "Line 1 to stdout"
      echo "Line 2 to stdout"
      echo "Line 3 to stdout"
      echo "stdout test complete" > stdout_marker.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_output_stdout.lua", spec)
        self.run_spec("local.ctx_run_output_stdout@v1", "ctx_run_output_stdout.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_output_stdout@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "stdout_marker.txt").exists())

    def test_output_stderr(self):
        """envy.run() captures stderr output."""
        spec = """-- Test envy.run() captures stderr output
IDENTITY = "local.ctx_run_output_stderr@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      [System.Console]::Error.WriteLine("Line 1 to stderr")
      [System.Console]::Error.WriteLine("Line 2 to stderr")
      Set-Content -Path stderr_marker.txt -Value "stderr test complete"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "Line 1 to stderr" >&2
      echo "Line 2 to stderr" >&2
      echo "stderr test complete" > stderr_marker.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_output_stderr.lua", spec)
        self.run_spec("local.ctx_run_output_stderr@v1", "ctx_run_output_stderr.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_output_stderr@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "stderr_marker.txt").exists())

    def test_output_large(self):
        """envy.run() with large output."""
        spec = """-- Test envy.run() with large output
IDENTITY = "local.ctx_run_output_large@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      for ($i = 1; $i -le 100; $i++) {{
        Write-Output "Output line $i with some content to make it longer"
      }}
      Set-Content -Path large_output_marker.txt -Value "Large output test complete"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      for i in {{1..100}}; do
        echo "Output line $i with some content to make it longer"
      done
      echo "Large output test complete" > large_output_marker.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_output_large.lua", spec)
        self.run_spec("local.ctx_run_output_large@v1", "ctx_run_output_large.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_output_large@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "large_output_marker.txt").exists())

    def test_output_multiline(self):
        """envy.run() with multi-line output."""
        spec = """-- Test envy.run() with multi-line output
IDENTITY = "local.ctx_run_output_multiline@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
$thisContent = @"
This is line 1
This is line 2
This is line 3

This is line 5 (after blank line)
	This line has a tab
    This line has spaces
"@
Set-Content -Path output.txt -Value $thisContent
Get-Content output.txt | ForEach-Object {{ Write-Output $_ }}
Set-Content -Path multiline_marker.txt -Value "Multi-line test complete"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
cat > output.txt <<'MULTILINE'
This is line 1
This is line 2
This is line 3

This is line 5 (after blank line)
	This line has a tab
    This line has spaces
MULTILINE

    cat output.txt
    echo "Multi-line test complete" > multiline_marker.txt
    ]])
  end
end
"""
        self.write_spec("ctx_run_output_multiline.lua", spec)
        self.run_spec("local.ctx_run_output_multiline@v1", "ctx_run_output_multiline.lua")
        pkg_path = self.get_pkg_path("local.ctx_run_output_multiline@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "multiline_marker.txt").exists())


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Functional tests for envy.run() in stage phase.

Tests comprehensive functionality of envy.run() including:
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

import hashlib
import io
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path
import unittest

from . import test_config

# Test archive contents - matches original test.tar.gz structure
TEST_ARCHIVE_FILES = {
    "root/file1.txt": "Hello, world!\n",
    "root/file2.txt": "Second file content\n",
}


def create_test_archive(output_path: Path) -> str:
    """Create test.tar.gz archive and return its SHA256 hash."""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tar:
        for name, content in TEST_ARCHIVE_FILES.items():
            data = content.encode("utf-8")
            info = tarfile.TarInfo(name=name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
    archive_data = buf.getvalue()
    output_path.write_bytes(archive_data)
    return hashlib.sha256(archive_data).hexdigest()


# Inline specs for ctx.run tests
SPECS = {
    "ctx_run_all_options.lua": '''-- Test envy.run() with all options combined
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
      $ErrorActionPreference = 'Continue'
      cmd /c exit 1
      Set-Content -Path all_opts_continued.txt -Value "Continued after false"
    ]], {{cwd = "subdir", env = {{MY_VAR = "test", ANOTHER = "value"}}, shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[mkdir -p subdir]])
    envy.run([[
      pwd > all_opts_pwd.txt
      echo "MY_VAR=$MY_VAR" > all_opts_env.txt
      echo "ANOTHER=$ANOTHER" >> all_opts_env.txt
      false || true
      echo "Continued after false" > all_opts_continued.txt
    ]], {{cwd = "subdir", env = {{MY_VAR = "test", ANOTHER = "value"}}}})
  end
end
''',
    "ctx_run_basic.lua": '''-- Test basic envy.run() execution
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
''',
    "ctx_run_check_false_capture.lua": '''-- Test envy.run() with check=false and capture returns exit_code and output
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
''',
    "ctx_run_check_false_nonzero.lua": '''-- Test envy.run() with check=false allows non-zero exit codes
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
''',
    "ctx_run_check_mode.lua": '''-- Test envy.run() check mode catches failures
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
''',
    "ctx_run_check_pipefail.lua": '''-- Test envy.run() check mode catches pipe failures
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
''',
    "ctx_run_check_undefined.lua": '''-- Test envy.run() check mode catches undefined variables
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
''',
    "ctx_run_command_not_found.lua": '''-- Test envy.run() error when command not found
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
''',
    "ctx_run_complex_conditional.lua": '''-- Test envy.run() with conditional operations
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
''',
    "ctx_run_complex_env_manip.lua": '''-- Test envy.run() with complex environment manipulation
IDENTITY = "local.ctx_run_complex_env_manip@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  -- First run with base environment
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

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path env_step3.txt -Value ("CONFIG_DIR=" + $env:CONFIG_DIR)
      Add-Content -Path env_step3.txt -Value ("DATA_DIR=" + $env:DATA_DIR)
      Add-Content -Path env_step3.txt -Value ("LOG_LEVEL=" + $env:LOG_LEVEL)
    ]], {{env = {{
      CONFIG_DIR = "C:/etc/myapp",
      DATA_DIR = "C:/var/lib/myapp",
      LOG_LEVEL = "debug"
    }}, shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      echo "CONFIG_DIR=$CONFIG_DIR" > env_step3.txt
      echo "DATA_DIR=$DATA_DIR" >> env_step3.txt
      echo "LOG_LEVEL=$LOG_LEVEL" >> env_step3.txt
    ]], {{env = {{
      CONFIG_DIR = "/etc/myapp",
      DATA_DIR = "/var/lib/myapp",
      LOG_LEVEL = "debug"
    }}}})
  end
end
''',
    "ctx_run_complex_loops.lua": '''-- Test envy.run() with loops and iterations
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
''',
    "ctx_run_complex_nested.lua": '''-- Test envy.run() with nested operations and complex scripts
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
''',
    "ctx_run_complex_workflow.lua": '''-- Test envy.run() with complex real-world workflow
IDENTITY = "local.ctx_run_complex_workflow@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    -- Consolidate workflow to avoid path issues and ensure directories created.
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
      echo "#define VERSION \"1.0.0\"" > src/version.h
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
''',
    "ctx_run_continue_after_failure.lua": '''-- Test envy.run() continues after a failing command
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
    ]], {{shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      false || true
      echo "This executes even after false" > continued.txt
    ]])
  end
end
''',
    "ctx_run_cwd_absolute.lua": '''-- Test envy.run() with absolute cwd path
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
''',
    "ctx_run_cwd_nested.lua": '''-- Test envy.run() with deeply nested relative cwd
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
''',
    "ctx_run_cwd_parent.lua": '''-- Test envy.run() with parent directory (..) in cwd
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
''',
    "ctx_run_cwd_relative.lua": '''-- Test envy.run() with relative cwd option
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
''',
    "ctx_run_edge_empty.lua": '''-- Test envy.run() with empty script
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
''',
    "ctx_run_edge_long_line.lua": '''-- Test envy.run() with very long lines
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
''',
    "ctx_run_edge_many_files.lua": '''-- Test envy.run() creating many files
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
''',
    "ctx_run_edge_slow_command.lua": '''-- Test envy.run() with slow command (ensures we wait for completion)
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
''',
    "ctx_run_edge_special_chars.lua": '''-- Test envy.run() with special characters
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
      Add-Content -Path special_chars.txt -Value 'Backslash: \ and newline: (literal)'
      if (-not (Test-Path special_chars.txt)) {{ exit 1 }}
      exit 0
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "Special chars: !@#$%^&*()_+-=[]{{}}|;:',.<>?/~\`" > special_chars.txt
      echo "Quotes: \"double\" 'single'" >> special_chars.txt
      echo "Backslash: \\ and newline: (literal)" >> special_chars.txt
    ]])
  end
end
''',
    "ctx_run_edge_unicode.lua": '''-- Test envy.run() with Unicode characters
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
''',
    "ctx_run_edge_whitespace.lua": '''-- Test envy.run() with only whitespace and comments
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
''',
    "ctx_run_env_complex.lua": '''-- Test envy.run() with complex environment variables
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
''',
    "ctx_run_env_custom.lua": '''-- Test envy.run() with custom environment variables
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
''',
    "ctx_run_env_empty.lua": '''-- Test envy.run() with empty env table still inherits
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
''',
    "ctx_run_env_inherit.lua": '''-- Test envy.run() inherits environment variables like PATH
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
''',
    "ctx_run_env_override.lua": '''-- Test envy.run() can override inherited environment variables
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
''',
    "ctx_run_error_nonzero.lua": '''-- Test envy.run() error on non-zero exit code
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
''',
    "ctx_run_file_ops.lua": '''-- Test envy.run() with file operations
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
''',
    "ctx_run_in_stage_dir.lua": '''-- Test envy.run() executes in stage_dir by default
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
''',
    "ctx_run_interleaved.lua": '''-- Test envy.run() interleaved with other operations
IDENTITY = "local.ctx_run_interleaved@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  -- Extract
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path steps.txt -Value "Step 1"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})

    envy.run([[
      if (Test-Path test.txt) {{
        Move-Item test.txt test_renamed.txt -Force
      }}
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})

    envy.run([[
      Add-Content -Path steps.txt -Value "Step 2"
      Get-ChildItem | Select-Object -ExpandProperty Name | Set-Content -Path file_list.txt
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})

    envy.run([[
      Set-Content -Path version.tmpl -Value "Version: {{{{version}}}}"
      $content = Get-Content version.tmpl
      $content = $content -replace "{{{{version}}}}", "1.0"
      Set-Content -Path version.txt -Value $content
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})

    envy.run([[
      Add-Content -Path steps.txt -Value "Step 3"
      $count = (Get-Content steps.txt | Measure-Object -Line).Lines
      Set-Content -Path step_count.txt -Value $count
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "Step 1" > steps.txt
    ]])

    envy.run([[
      if [ -f test.txt ]; then
        mv test.txt test_renamed.txt
      fi
    ]])

    envy.run([[
      echo "Step 2" >> steps.txt
      ls > file_list.txt
    ]])

    envy.run([[
      echo "Version: {{{{version}}}}" > version.tmpl
      version="1.0"
      sed "s/{{{{version}}}}/$version/g" version.tmpl > version.txt
    ]])

    envy.run([[
      echo "Step 3" >> steps.txt
      wc -l steps.txt > step_count.txt
    ]])
  end
end
''',
    "ctx_run_invalid_cwd.lua": '''-- Test envy.run() error when cwd doesn't exist
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
''',
    "ctx_run_lua_bad_args.lua": '''-- Test envy.run() with invalid arguments (Lua error)
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
''',
    "ctx_run_lua_bad_opts.lua": '''-- Test envy.run() with invalid options (Lua error)
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
''',
    "ctx_run_lua_error_after.lua": '''-- Test Lua error after envy.run() succeeds
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
''',
    "ctx_run_lua_error_before.lua": '''-- Test Lua error before envy.run()
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
''',
    "ctx_run_multiple_calls.lua": '''-- Test multiple envy.run() calls in sequence
IDENTITY = "local.ctx_run_multiple_calls@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path call1.txt -Value "Call 1"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
    envy.run([[
      Set-Content -Path call2.txt -Value "Call 2"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
    envy.run([[
      Set-Content -Path call3.txt -Value "Call 3"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
    envy.run([[
      Get-Content call1.txt, call2.txt, call3.txt | Set-Content -Path all_calls.txt
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "Call 1" > call1.txt
    ]])
    envy.run([[
      echo "Call 2" > call2.txt
    ]])
    envy.run([[
      echo "Call 3" > call3.txt
    ]])
    envy.run([[
      cat call1.txt call2.txt call3.txt > all_calls.txt
    ]])
  end
end
''',
    "ctx_run_multiple_cmds.lua": '''-- Test envy.run() with multiple commands
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
''',
    "ctx_run_option_combinations.lua": '''-- Test envy.run() with various option combinations
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
    ]], {{cwd = "dir2", shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      pwd > combo2_pwd.txt
      false || true
      echo "After false" > combo2_continued.txt
    ]], {{cwd = "dir2"}})
  end

  -- Combination 3: env with a failing command (default cwd)
  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path combo3_env.txt -Value ("VAR2=" + $env:VAR2)
      cmd /c exit 1
      Add-Content -Path combo3_env.txt -Value "Continued"
    ]], {{env = {{VAR2 = "value2"}}, shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      echo "VAR2=$VAR2" > combo3_env.txt
      false || true
      echo "Continued" >> combo3_env.txt
    ]], {{env = {{VAR2 = "value2"}}}})
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
    ]], {{shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      false || true
      echo "Standalone failure scenario" > combo6_continued.txt
    ]])
  end
end

''',
    "ctx_run_output_large.lua": '''-- Test envy.run() with large output
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
''',
    "ctx_run_output_multiline.lua": '''-- Test envy.run() with multi-line output
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
''',
    "ctx_run_output_stderr.lua": '''-- Test envy.run() captures stderr output
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
''',
    "ctx_run_output_stdout.lua": '''-- Test envy.run() captures stdout output
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
''',
    "ctx_run_shell_cmd.lua": '''-- Verify envy.run() with shell="cmd" on Windows hosts
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
''',
    "ctx_run_shell_sh.lua": '''-- Verify envy.run() with shell="sh" on POSIX hosts
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

  envy.run([[\
    set -eu\
    printf "shell=sh\n" > shell_sh_marker.txt\
  ]], {{ shell = ENVY_SHELL.SH }})
end
''',
    "ctx_run_signal_term.lua": '''-- Test envy.run() error on signal termination
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
''',
    "ctx_run_stage_archiving.lua": '''-- Test envy.run() in stage for creating archives
IDENTITY = "local.ctx_run_stage_archiving@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  -- Create archives
  if envy.PLATFORM == "windows" then
    envy.run([[
      New-Item -ItemType Directory -Force -Path "archive_test/subdir" | Out-Null
      Set-Content -Path archive_test/file1.txt -Value "file1"
      Set-Content -Path archive_test/subdir/file2.txt -Value "file2"
      Compress-Archive -Path archive_test -DestinationPath archive_test.zip -Force
      if (Test-Path archive_test.zip) {{
        Set-Content -Path archive_log.txt -Value "Archive created"
      }} else {{
        exit 1
      }}
      if (-not (Test-Path archive_log.txt)) {{ exit 1 }}
      exit 0
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      # Create a directory structure
      mkdir -p archive_test/subdir
      echo "file1" > archive_test/file1.txt
      echo "file2" > archive_test/subdir/file2.txt

      # Create tar archive
      tar czf archive_test.tar.gz archive_test/

      # Verify archive was created
      test -f archive_test.tar.gz && echo "Archive created" > archive_log.txt
    ]])
  end
end
''',
    "ctx_run_stage_build_prep.lua": '''-- Test envy.run() in stage for build preparation
IDENTITY = "local.ctx_run_stage_build_prep@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      New-Item -ItemType Directory -Force -Path build | Out-Null
      Push-Location build
      Set-Content -Path config.txt -Value "# Build configuration"
      Add-Content -Path config.txt -Value "CFLAGS=-O2"
      Pop-Location
      if (-not (Test-Path build/config.txt)) {{ exit 1 }}
      exit 0
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      mkdir -p build
      cd build
      echo "# Build configuration" > config.txt
      echo "CFLAGS=-O2" >> config.txt
    ]])
  end
end
''',
    "ctx_run_stage_cleanup.lua": '''-- Test envy.run() in stage for cleanup operations
IDENTITY = "local.ctx_run_stage_cleanup@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Get-ChildItem -Recurse -Filter *.bak | Remove-Item -Force -ErrorAction SilentlyContinue
      Get-ChildItem -Recurse -Filter *.tmp | Remove-Item -Force -ErrorAction SilentlyContinue
      Get-ChildItem -Recurse -Directory | Where-Object {{ $_.GetFileSystemInfos().Count -eq 0 }} | Remove-Item -Force -ErrorAction SilentlyContinue
      Set-Content -Path cleanup_log.txt -Value "Cleanup complete"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      find . -name "*.bak" -delete
      rm -f *.tmp
      find . -type d -empty -delete
      echo "Cleanup complete" > cleanup_log.txt
    ]])
  end
end
''',
    "ctx_run_stage_compilation.lua": '''-- Test envy.run() in stage for simple compilation
IDENTITY = "local.ctx_run_stage_compilation@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
$source = @'
#include <stdio.h>
int main() {{ printf("Hello\n"); return 0; }}
'@
Set-Content -Path hello.c -Value $source
Set-Content -Path compile_log.txt -Value "Compiling hello.c..."
Add-Content -Path compile_log.txt -Value "Compilation successful"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
cat > hello.c <<'EOF'
#include <stdio.h>
int main() {{ printf("Hello\n"); return 0; }}
EOF
echo "Compiling hello.c..." > compile_log.txt
echo "Compilation successful" >> compile_log.txt
    ]])
  end
end
''',
    "ctx_run_stage_generation.lua": '''-- Test envy.run() in stage for code generation
IDENTITY = "local.ctx_run_stage_generation@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
$header = @"
#ifndef GENERATED_H
#define GENERATED_H
#define VERSION \"1.0.0\"
#endif
"@
Set-Content -Path generated.h -Value $header
Set-Content -Path generated.bat -Value "@echo off`necho Generated script"
# Also produce generated.sh for test parity
Set-Content -Path generated.sh -Value "echo Generated script"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
cat > generated.h <<EOF
#ifndef GENERATED_H
#define GENERATED_H
#define VERSION "1.0.0"
#endif
EOF

cat > generated.sh <<'SCRIPT'
#!/usr/bin/env bash
echo "Generated script"
SCRIPT
chmod +x generated.sh
    ]])
  end
end
''',
    "ctx_run_stage_patch.lua": '''-- Test envy.run() in stage for patching
IDENTITY = "local.ctx_run_stage_patch@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path patch_log.txt -Value "Patching file"
      Set-Content -Path temp.txt -Value "old content"
      (Get-Content temp.txt) -replace "old","new" | Set-Content -Path temp.txt
      Add-Content -Path patch_log.txt -Value "Patch applied"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "Patching file" > patch_log.txt
      echo "old content" > temp.txt
      sed 's/old/new/g' temp.txt > temp.txt.patched
      mv temp.txt.patched temp.txt
      echo "Patch applied" >> patch_log.txt
    ]])
  end
end
''',
    "ctx_run_stage_permissions.lua": '''-- Test envy.run() in stage for setting permissions
IDENTITY = "local.ctx_run_stage_permissions@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      (Get-Item file1.txt).Attributes = 'Normal'
      Add-Content -Path permissions.txt -Value ((Get-Item file1.txt).Attributes)
      Set-Content -Path executable.bat -Value "@echo off"
      (Get-Item executable.bat).Attributes = 'Normal'
      Add-Content -Path permissions.txt -Value ((Get-Item executable.bat).Attributes)
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      chmod +x file1.txt
      ls -l file1.txt > permissions.txt
      touch executable.sh
      chmod 755 executable.sh
      ls -l executable.sh >> permissions.txt
    ]])
  end
end
''',
    "ctx_run_stage_verification.lua": '''-- Test envy.run() in stage for verification checks
IDENTITY = "local.ctx_run_stage_verification@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      if (-not (Test-Path file1.txt)) {{ throw "Missing file1.txt" }}
      if ((Get-Item file1.txt).Length -eq 0) {{ throw "File is empty" }}
      Set-Content -Path verification.txt -Value "All verification checks passed"
      if (-not (Test-Path verification.txt)) {{ throw "verification.txt missing post write" }}
      exit 0
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      test -f file1.txt || (echo "Missing file1.txt" && exit 1)
      test -s file1.txt || (echo "File is empty" && exit 1)
      echo "All verification checks passed" > verification.txt
    ]])
  end
end
''',
    "ctx_run_with_extract.lua": '''-- Test envy.run() mixed with envy.extract_all()
IDENTITY = "local.ctx_run_with_extract@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  -- Extract first
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
      envy.run([[
        Get-ChildItem -Name | Set-Content -Path extracted_files.txt
        if (Test-Path file1.txt) {{ Set-Content -Path verify_extract.txt -Value "Extraction verified" }} else {{ exit 52 }}
        Add-Content -Path file1.txt -Value "Modified by ctx.run"
      ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      ls > extracted_files.txt
      test -f file1.txt && echo "Extraction verified" > verify_extract.txt
    ]])

    envy.run([[
      echo "Modified by ctx.run" >> file1.txt
    ]])
  end
end
''',
    "ctx_run_with_pipes.lua": '''-- Test envy.run() with shell pipes and redirection
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
      echo -e "line3\nline1\nline2" | sort > sorted.txt
      cat sorted.txt | grep "line2" > grepped.txt
      echo "Pipes work" >> grepped.txt
    ]])
  end
end
''',
    "ctx_run_with_rename.lua": '''-- Test envy.run() mixed with envy.rename()
IDENTITY = "local.ctx_run_with_rename@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path original.txt -Value "original name"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})

    envy.run([[
      Move-Item -Path original.txt -Destination renamed.txt -Force
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})

    envy.run([[
      if (Test-Path renamed.txt) {{ Set-Content -Path rename_check.txt -Value "Rename verified" }}
      if (-not (Test-Path original.txt)) {{ Add-Content -Path rename_check.txt -Value "Original gone" }}
      if (-not (Test-Path rename_check.txt)) {{ Write-Error "rename_check missing"; exit 1 }}
      exit 0
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "original name" > original.txt
    ]])

    envy.run([[
      mv original.txt renamed.txt
    ]])

    envy.run([[
      test -f renamed.txt && echo "Rename verified" > rename_check.txt
      test ! -f original.txt && echo "Original gone" >> rename_check.txt
    ]])
  end
end
''',
    "ctx_run_with_template.lua": '''-- Test envy.run() mixed with envy.template()
IDENTITY = "local.ctx_run_with_template@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path greeting.tmpl -Value "Hello {{{{name}}}}, you are {{{{age}}}} years old"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})

    envy.run([[
      $content = Get-Content greeting.tmpl
      $content = $content -replace "{{{{name}}}}","Alice" -replace "{{{{age}}}}","30"
      Set-Content -Path greeting.txt -Value $content
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})

    envy.run([[
      if ((Select-String -Path greeting.txt -Pattern "Alice")) {{ Set-Content -Path template_check.txt -Value "Alice" }}
      if ((Select-String -Path greeting.txt -Pattern "30")) {{ Add-Content -Path template_check.txt -Value "30" }}
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "Hello {{{{name}}}}, you are {{{{age}}}} years old" > greeting.tmpl
    ]])

    envy.run([[
      name="Alice"
      age="30"
      sed "s/{{{{name}}}}/$name/g; s/{{{{age}}}}/$age/g" greeting.tmpl > greeting.txt
    ]])

    envy.run([[
      grep "Alice" greeting.txt > template_check.txt
      grep "30" greeting.txt >> template_check.txt
    ]])
  end
end
''',
}


class TestCtxRun(unittest.TestCase):
    """Tests for envy.run() functionality."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-ctx-run-test-"))
        self.specs_dir = Path(tempfile.mkdtemp(prefix="envy-ctx-run-specs-"))
        self.envy_test = test_config.get_envy_executable()
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

        # Create test archive and get its hash
        self.archive_path = self.specs_dir / "test.tar.gz"
        self.archive_hash = create_test_archive(self.archive_path)
        self.archive_lua_path = self.archive_path.as_posix()

        # Write specs to temp directory with actual archive path/hash
        for name, content in SPECS.items():
            spec_content = content.format(
                ARCHIVE_PATH=self.archive_lua_path,
                ARCHIVE_HASH=self.archive_hash
            )
            (self.specs_dir / name).write_text(spec_content, encoding="utf-8")

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.specs_dir, ignore_errors=True)

    def get_pkg_path(self, identity):
        """Find package directory for given identity in cache."""
        pkgs_dir = self.cache_root / "packages" / identity
        if not pkgs_dir.exists():
            return None
        for subdir in pkgs_dir.iterdir():
            if subdir.is_dir():
                pkg_dir = subdir / "pkg"
                if pkg_dir.exists():
                    return pkg_dir
                return subdir
        return None

    def run_spec(self, identity, spec_name, should_fail=False):
        """Run a spec and return result."""
        spec_path = str(self.specs_dir / spec_name)
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                identity,
                spec_path,
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
        """envy.run() executes shell commands successfully."""
        self.run_spec(
            "local.ctx_run_basic@v1",
            "ctx_run_basic.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_basic@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "run_marker.txt").exists())

    def test_multiple_commands(self):
        """envy.run() executes multiple commands in sequence."""
        self.run_spec(
            "local.ctx_run_multiple_cmds@v1",
            "ctx_run_multiple_cmds.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_multiple_cmds@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "all_cmds.txt").exists())

    def test_file_operations(self):
        """envy.run() can perform file operations."""
        self.run_spec(
            "local.ctx_run_file_ops@v1",
            "ctx_run_file_ops.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_file_ops@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "ops_result.txt").exists())

    def test_runs_in_stage_dir(self):
        """envy.run() executes in stage directory by default."""
        self.run_spec(
            "local.ctx_run_in_stage_dir@v1",
            "ctx_run_in_stage_dir.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_in_stage_dir@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "stage_verification.txt").exists())

    def test_with_pipes(self):
        """envy.run() supports shell pipes and redirection."""
        self.run_spec(
            "local.ctx_run_with_pipes@v1",
            "ctx_run_with_pipes.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_with_pipes@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "grepped.txt").exists())

    # ===== Error Handling Tests =====

    def test_error_nonzero_exit(self):
        """envy.run() fails on non-zero exit code."""
        self.run_spec(
            "local.ctx_run_error_nonzero@v1",
            "ctx_run_error_nonzero.lua",
            should_fail=True,
        )

    def test_check_mode_catches_failures(self):
        """envy.run() check mode catches command failures."""
        self.run_spec(
            "local.ctx_run_check_mode@v1",
            "ctx_run_check_mode.lua",
            should_fail=True,
        )

    @unittest.skipIf(sys.platform == "win32", "Signals not supported on Windows")
    def test_signal_termination(self):
        """envy.run() reports signal termination."""
        self.run_spec(
            "local.ctx_run_signal_term@v1",
            "ctx_run_signal_term.lua",
            should_fail=True,
        )

    def test_invalid_cwd(self):
        """envy.run() fails when cwd doesn't exist."""
        self.run_spec(
            "local.ctx_run_invalid_cwd@v1",
            "ctx_run_invalid_cwd.lua",
            should_fail=True,
        )

    def test_command_not_found(self):
        """envy.run() fails when command doesn't exist."""
        self.run_spec(
            "local.ctx_run_command_not_found@v1",
            "ctx_run_command_not_found.lua",
            should_fail=True,
        )

    @unittest.skipUnless(os.name != "nt", "requires POSIX shells")
    def test_shell_sh(self):
        """envy.run() executes explicitly via /bin/sh."""
        self.run_spec(
            "local.ctx_run_shell_sh@v1",
            "ctx_run_shell_sh.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_shell_sh@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "shell_sh_marker.txt").exists())

    @unittest.skipUnless(os.name == "nt", "requires Windows CMD")
    def test_shell_cmd(self):
        """envy.run() executes explicitly via cmd.exe."""
        self.run_spec(
            "local.ctx_run_shell_cmd@v1",
            "ctx_run_shell_cmd.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_shell_cmd@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "shell_cmd_marker.txt").exists())

    # ===== CWD Tests =====

    def test_cwd_relative(self):
        """envy.run() supports relative cwd paths."""
        self.run_spec(
            "local.ctx_run_cwd_relative@v1",
            "ctx_run_cwd_relative.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_cwd_relative@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "custom" / "subdir" / "marker.txt").exists())

    def test_cwd_absolute(self):
        """envy.run() supports absolute cwd paths."""
        self.run_spec(
            "local.ctx_run_cwd_absolute@v1",
            "ctx_run_cwd_absolute.lua",
        )
        if os.name == "nt":
            target = Path(os.environ.get("TEMP", "C:/Temp")) / "envy_ctx_run_test.txt"
        else:
            target = Path("/tmp/envy_ctx_run_test.txt")
        self.assertTrue(target.exists())
        target.unlink(missing_ok=True)

    def test_cwd_parent(self):
        """envy.run() handles parent directory (..) in cwd."""
        self.run_spec(
            "local.ctx_run_cwd_parent@v1",
            "ctx_run_cwd_parent.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_cwd_parent@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "deep" / "parent_marker.txt").exists())

    def test_cwd_nested(self):
        """envy.run() handles deeply nested relative cwd."""
        self.run_spec(
            "local.ctx_run_cwd_nested@v1",
            "ctx_run_cwd_nested.lua",
        )
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

    # ===== Check Mode Tests =====

    def test_continue_after_failure(self):
        """envy.run() continues execution after a failing command."""
        self.run_spec(
            "local.ctx_run_continue_after_failure@v1",
            "ctx_run_continue_after_failure.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_continue_after_failure@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "continued.txt").exists())

    def test_check_undefined_variable(self):
        """envy.run() check mode catches undefined variables."""
        self.run_spec(
            "local.ctx_run_check_undefined@v1",
            "ctx_run_check_undefined.lua",
            should_fail=True,
        )

    def test_check_pipefail(self):
        """envy.run() check mode catches pipe failures."""
        self.run_spec(
            "local.ctx_run_check_pipefail@v1",
            "ctx_run_check_pipefail.lua",
            should_fail=True,
        )

    def test_check_false_nonzero(self):
        """envy.run() with check=false allows non-zero exit codes."""
        self.run_spec(
            "local.ctx_run_check_false_nonzero@v1",
            "ctx_run_check_false_nonzero.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_check_false_nonzero@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "continued_after_failure.txt").exists())

    def test_check_false_capture(self):
        """envy.run() with check=false and capture returns exit_code and output."""
        self.run_spec(
            "local.ctx_run_check_false_capture@v1",
            "ctx_run_check_false_capture.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_check_false_capture@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "capture_success.txt").exists())

    # ===== Environment Tests =====

    def test_env_custom(self):
        """envy.run() supports custom environment variables."""
        self.run_spec(
            "local.ctx_run_env_custom@v1",
            "ctx_run_env_custom.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_env_custom@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "env_output.txt").exists())
        content = (pkg_path / "env_output.txt").read_text()
        self.assertIn("MY_VAR=test_value", content)
        self.assertIn("MY_NUM=42", content)

    def test_env_inherit(self):
        """envy.run() inherits environment variables like PATH."""
        self.run_spec(
            "local.ctx_run_env_inherit@v1",
            "ctx_run_env_inherit.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_env_inherit@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "path_verification.txt").exists())

    def test_env_override(self):
        """envy.run() can override inherited environment variables."""
        self.run_spec(
            "local.ctx_run_env_override@v1",
            "ctx_run_env_override.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_env_override@v1")
        assert pkg_path
        content = (pkg_path / "overridden_user.txt").read_text()
        self.assertIn("USER=test_override_user", content)

    def test_env_empty(self):
        """envy.run() with empty env table still inherits."""
        self.run_spec(
            "local.ctx_run_env_empty@v1",
            "ctx_run_env_empty.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_env_empty@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "empty_env.txt").exists())

    def test_env_complex(self):
        """envy.run() handles complex environment values."""
        self.run_spec(
            "local.ctx_run_env_complex@v1",
            "ctx_run_env_complex.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_env_complex@v1")
        assert pkg_path
        content = (pkg_path / "env_complex.txt").read_text()
        self.assertIn("STRING=hello_world", content)
        self.assertIn("NUMBER=12345", content)
        self.assertIn("WITH_SPACE=value with spaces", content)

    # ===== Integration Tests =====

    def test_with_extract(self):
        """envy.run() works with envy.extract_all()."""
        self.run_spec(
            "local.ctx_run_with_extract@v1",
            "ctx_run_with_extract.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_with_extract@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "verify_extract.txt").exists())

    def test_with_rename(self):
        """envy.run() works with ctx.rename()."""
        self.run_spec(
            "local.ctx_run_with_rename@v1",
            "ctx_run_with_rename.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_with_rename@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "rename_check.txt").exists())

    def test_with_template(self):
        """envy.run() works with envy.template()."""
        self.run_spec(
            "local.ctx_run_with_template@v1",
            "ctx_run_with_template.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_with_template@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "template_check.txt").exists())

    def test_multiple_calls(self):
        """Multiple envy.run() calls work in sequence."""
        self.run_spec(
            "local.ctx_run_multiple_calls@v1",
            "ctx_run_multiple_calls.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_multiple_calls@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "all_calls.txt").exists())

    def test_interleaved(self):
        """envy.run() interleaves with other operations."""
        self.run_spec(
            "local.ctx_run_interleaved@v1",
            "ctx_run_interleaved.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_interleaved@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "step_count.txt").exists())

    # ===== Phase-Specific Tests =====

    def test_stage_build_prep(self):
        """envy.run() in stage for build preparation."""
        self.run_spec(
            "local.ctx_run_stage_build_prep@v1",
            "ctx_run_stage_build_prep.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_stage_build_prep@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "build" / "config.txt").exists())

    def test_stage_patch(self):
        """envy.run() in stage for patching."""
        self.run_spec(
            "local.ctx_run_stage_patch@v1",
            "ctx_run_stage_patch.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_stage_patch@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "patch_log.txt").exists())

    def test_stage_permissions(self):
        """envy.run() in stage for setting permissions."""
        self.run_spec(
            "local.ctx_run_stage_permissions@v1",
            "ctx_run_stage_permissions.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_stage_permissions@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "permissions.txt").exists())

    def test_stage_verification(self):
        """envy.run() in stage for verification checks."""
        self.run_spec(
            "local.ctx_run_stage_verification@v1",
            "ctx_run_stage_verification.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_stage_verification@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "verification.txt").exists())

    def test_stage_generation(self):
        """envy.run() in stage for code generation."""
        self.run_spec(
            "local.ctx_run_stage_generation@v1",
            "ctx_run_stage_generation.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_stage_generation@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "generated.h").exists())
        self.assertTrue((pkg_path / "generated.sh").exists())

    def test_stage_cleanup(self):
        """envy.run() in stage for cleanup operations."""
        self.run_spec(
            "local.ctx_run_stage_cleanup@v1",
            "ctx_run_stage_cleanup.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_stage_cleanup@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "cleanup_log.txt").exists())

    def test_stage_compilation(self):
        """envy.run() in stage for compilation."""
        self.run_spec(
            "local.ctx_run_stage_compilation@v1",
            "ctx_run_stage_compilation.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_stage_compilation@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "compile_log.txt").exists())

    def test_stage_archiving(self):
        """envy.run() in stage for creating archives."""
        self.run_spec(
            "local.ctx_run_stage_archiving@v1",
            "ctx_run_stage_archiving.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_stage_archiving@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "archive_log.txt").exists())

    # ===== Output/Logging Tests =====

    def test_output_stdout(self):
        """envy.run() captures stdout output."""
        self.run_spec(
            "local.ctx_run_output_stdout@v1",
            "ctx_run_output_stdout.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_output_stdout@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "stdout_marker.txt").exists())

    def test_output_stderr(self):
        """envy.run() captures stderr output."""
        self.run_spec(
            "local.ctx_run_output_stderr@v1",
            "ctx_run_output_stderr.lua",
        )
        # Stderr should be captured in logs
        pkg_path = self.get_pkg_path("local.ctx_run_output_stderr@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "stderr_marker.txt").exists())

    def test_output_large(self):
        """envy.run() handles large output."""
        self.run_spec(
            "local.ctx_run_output_large@v1",
            "ctx_run_output_large.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_output_large@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "large_output_marker.txt").exists())

    def test_output_multiline(self):
        """envy.run() handles multi-line output."""
        self.run_spec(
            "local.ctx_run_output_multiline@v1",
            "ctx_run_output_multiline.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_output_multiline@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "multiline_marker.txt").exists())

    # ===== Complex Scenario Tests =====

    def test_complex_workflow(self):
        """envy.run() handles complex real-world workflow."""
        self.run_spec(
            "local.ctx_run_complex_workflow@v1",
            "ctx_run_complex_workflow.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_complex_workflow@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "workflow_complete.txt").exists())

    def test_complex_env_manipulation(self):
        """envy.run() handles complex environment manipulation."""
        self.run_spec(
            "local.ctx_run_complex_env_manip@v1",
            "ctx_run_complex_env_manip.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_complex_env_manip@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "env_step1.txt").exists())
        self.assertTrue((pkg_path / "env_step2.txt").exists())
        self.assertTrue((pkg_path / "env_step3.txt").exists())

    def test_complex_conditional(self):
        """envy.run() handles conditional operations."""
        self.run_spec(
            "local.ctx_run_complex_conditional@v1",
            "ctx_run_complex_conditional.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_complex_conditional@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "os_info.txt").exists())
        self.assertTrue((pkg_path / "test_info.txt").exists())

    def test_complex_loops(self):
        """envy.run() handles loops and iterations."""
        self.run_spec(
            "local.ctx_run_complex_loops@v1",
            "ctx_run_complex_loops.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_complex_loops@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "loop_output.txt").exists())

    def test_complex_nested(self):
        """envy.run() handles nested operations."""
        self.run_spec(
            "local.ctx_run_complex_nested@v1",
            "ctx_run_complex_nested.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_complex_nested@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "summary.txt").exists())

    # ===== Edge Case Tests =====

    def test_edge_empty_script(self):
        """envy.run() handles empty script."""
        self.run_spec(
            "local.ctx_run_edge_empty@v1",
            "ctx_run_edge_empty.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_edge_empty@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "after_empty.txt").exists())

    def test_edge_whitespace(self):
        """envy.run() handles whitespace-only script."""
        self.run_spec(
            "local.ctx_run_edge_whitespace@v1",
            "ctx_run_edge_whitespace.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_edge_whitespace@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "after_whitespace.txt").exists())

    def test_edge_long_line(self):
        """envy.run() handles very long lines."""
        self.run_spec(
            "local.ctx_run_edge_long_line@v1",
            "ctx_run_edge_long_line.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_edge_long_line@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "long_line.txt").exists())

    def test_edge_special_chars(self):
        """envy.run() handles special characters."""
        self.run_spec(
            "local.ctx_run_edge_special_chars@v1",
            "ctx_run_edge_special_chars.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_edge_special_chars@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "special_chars.txt").exists())

    def test_edge_unicode(self):
        """envy.run() handles Unicode characters."""
        self.run_spec(
            "local.ctx_run_edge_unicode@v1",
            "ctx_run_edge_unicode.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_edge_unicode@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "unicode.txt").exists())

    def test_edge_many_files(self):
        """envy.run() handles creating many files."""
        self.run_spec(
            "local.ctx_run_edge_many_files@v1",
            "ctx_run_edge_many_files.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_edge_many_files@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "many_files_marker.txt").exists())

    def test_edge_slow_command(self):
        """envy.run() waits for slow commands."""
        self.run_spec(
            "local.ctx_run_edge_slow_command@v1",
            "ctx_run_edge_slow_command.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_edge_slow_command@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "slow_verify.txt").exists())

    # ===== Lua Error Integration Tests =====

    def test_lua_error_after(self):
        """Lua error after envy.run() succeeds."""
        self.run_spec(
            "local.ctx_run_lua_error_after@v1",
            "ctx_run_lua_error_after.lua",
            should_fail=True,
        )

    def test_lua_error_before(self):
        """Lua error before envy.run()."""
        self.run_spec(
            "local.ctx_run_lua_error_before@v1",
            "ctx_run_lua_error_before.lua",
            should_fail=True,
        )

    def test_lua_bad_args(self):
        """envy.run() with invalid arguments."""
        self.run_spec(
            "local.ctx_run_lua_bad_args@v1",
            "ctx_run_lua_bad_args.lua",
            should_fail=True,
        )

    def test_lua_bad_opts(self):
        """envy.run() with invalid options."""
        self.run_spec(
            "local.ctx_run_lua_bad_opts@v1",
            "ctx_run_lua_bad_options.lua",
            should_fail=True,
        )

    # ===== Option Combination Tests =====

    def test_all_options(self):
        """envy.run() with all options combined."""
        self.run_spec(
            "local.ctx_run_all_options@v1",
            "ctx_run_all_options.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_all_options@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "subdir" / "all_opts_continued.txt").exists())

    def test_option_combinations(self):
        """envy.run() with various option combinations."""
        self.run_spec(
            "local.ctx_run_option_combinations@v1",
            "ctx_run_option_combinations.lua",
        )
        pkg_path = self.get_pkg_path("local.ctx_run_option_combinations@v1")
        assert pkg_path
        self.assertTrue((pkg_path / "dir1" / "combo1_env.txt").exists())
        self.assertTrue((pkg_path / "dir2" / "combo2_continued.txt").exists())
        self.assertTrue((pkg_path / "combo3_env.txt").exists())


if __name__ == "__main__":
    unittest.main()

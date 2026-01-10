#!/usr/bin/env python3
"""Functional tests for engine build phase.

Tests build phase with nil, string, and function forms. Verifies ctx API
(run, package, copy, move, extract) and build phase integration.
"""

import hashlib
import io
import os
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path
import unittest

from . import test_config

# Test archive contents
TEST_ARCHIVE_FILES = {
    "root/file1.txt": "Root file content\n",
    "root/file2.txt": "Another root file\n",
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


# Inline specs for build tests - use {ARCHIVE_PATH} and {ARCHIVE_HASH} placeholders
SPECS = {
    "build_nil.lua": """-- Test build phase: build = nil (skip build)
IDENTITY = "local.build_nil@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

-- No build field - should skip build phase and proceed to install
""",
    "build_string.lua": """-- Test build phase: build = "shell script" (shell execution)
IDENTITY = "local.build_string@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  if envy.PLATFORM == "windows" then
    envy.run([[
      Write-Host "Building in shell script mode"
      New-Item -ItemType Directory -Path build_output -Force | Out-Null
      Set-Content -Path build_output/artifact.txt -Value "build_artifact"
      Get-ChildItem
      if (-not (Test-Path build_output/artifact.txt)) {{ Write-Error "artifact missing"; exit 1 }}
      Write-Output "Build string shell success"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "Building in shell script mode"
      mkdir -p build_output
      echo "build_artifact" > build_output/artifact.txt
      ls -la
    ]])
  end
end
""",
    "build_function.lua": """-- Test build phase: build = function(ctx, opts) (programmatic with envy.run())
IDENTITY = "local.build_function@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Building with envy.run()")

  -- Create build artifacts
  local result
  if envy.PLATFORM == "windows" then
    result = envy.run([[mkdir build_output 2> nul & echo function_artifact > build_output\\result.txt & if not exist build_output\\result.txt ( echo Artifact missing & exit /b 1 ) & echo Build complete & exit /b 0 ]],
                     {{ shell = ENVY_SHELL.CMD, capture = true }})
  else
    result = envy.run([[
      mkdir -p build_output
      echo "function_artifact" > build_output/result.txt
      echo "Build complete"
    ]],
                     {{ capture = true }})
  end

  -- Verify stdout contains expected output
  if not result.stdout:match("Build complete") then
    error("Expected 'Build complete' in stdout")
  end

  print("Build finished successfully")
end
""",
    "build_dependency.lua": """-- Test dependency for build_with_asset
IDENTITY = "local.build_dependency@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  if envy.PLATFORM == "windows" then
    envy.run([[Write-Output "dependency: begin"; Remove-Item -Force dependency.txt -ErrorAction SilentlyContinue; Set-Content -Path dependency.txt -Value "dependency_data"; New-Item -ItemType Directory -Path bin -Force | Out-Null; Set-Content -Path bin/app -Value "binary"; if (-not (Test-Path bin/app)) {{ Write-Error "missing bin/app"; exit 1 }}; Write-Output "dependency: success"; exit 0 ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[echo 'dependency_data' > dependency.txt
      mkdir -p bin
      echo 'binary' > bin/app]])
  end
end
""",
    "build_with_package.lua": """-- Test build phase: envy.package() for dependency access
IDENTITY = "local.build_with_package@v1"

DEPENDENCIES = {{
  {{ spec = "local.build_dependency@v1", source = "{SPECS_DIR}/build_dependency.lua" }}
}}

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Accessing dependency via envy.package()")

  local dep_path = envy.package("local.build_dependency@v1")
  print("Dependency path: " .. dep_path)

  -- Copy dependency file
  local result
  if envy.PLATFORM == "windows" then
    result = envy.run([[
      $depFile = ']] .. dep_path .. [[/dependency.txt'
      if (-not (Test-Path $depFile)) {{ Start-Sleep -Milliseconds 100 }}
      if (-not (Test-Path $depFile)) {{ Write-Error "Dependency artifact missing"; exit 61 }}
      Get-Content $depFile | Set-Content -Path from_dependency.txt
      Write-Output "Used dependency data"
      if (-not (Test-Path from_dependency.txt)) {{ Write-Error "Output artifact missing"; exit 62 }}
      exit 0
    ]],
                     {{ shell = ENVY_SHELL.POWERSHELL, capture = true }})
  else
    result = envy.run([[
      cat "]] .. dep_path .. [[/dependency.txt" > from_dependency.txt
      echo "Used dependency data"
    ]],
                     {{ capture = true }})
  end

  if not result.stdout:match("Used dependency data") then
    error("Failed to use dependency")
  end
end
""",
    "build_with_copy.lua": """-- Test build phase: envy.copy() for file and directory copy
IDENTITY = "local.build_with_copy@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Testing envy.copy()")

  -- Create source files
  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path source.txt -Value "source_file"
      New-Item -ItemType Directory -Path source_dir -Force | Out-Null
      Set-Content -Path source_dir/file1.txt -Value "nested1"
      Set-Content -Path source_dir/file2.txt -Value "nested2"
      Write-Output "creation_done"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "source_file" > source.txt
      mkdir -p source_dir
      echo "nested1" > source_dir/file1.txt
      echo "nested2" > source_dir/file2.txt
    ]])
  end

  -- Copy single file
  envy.copy("source.txt", "dest_file.txt")

  -- Copy directory recursively
  envy.copy("source_dir", "dest_dir")

  -- Verify copies
  if envy.PLATFORM == "windows" then
    envy.run([[
      if (-not (Test-Path dest_file.txt)) {{
        Write-Output "missing dest_file.txt"
        exit 1
      }}
      if (-not (Test-Path dest_dir/file1.txt)) {{
        Write-Output "missing file1.txt"
        exit 1
      }}
      if (-not (Test-Path dest_dir/file2.txt)) {{
        Write-Output "missing file2.txt"
        exit 1
      }}
      Write-Output "Copy operations successful"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      test -f dest_file.txt || exit 1
      test -f dest_dir/file1.txt || exit 1
      test -f dest_dir/file2.txt || exit 1
      echo "Copy operations successful"
    ]])
  end
end
""",
    "build_with_move.lua": """-- Test build phase: envy.move() for efficient rename operations
IDENTITY = "local.build_with_move@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Testing envy.move()")

  -- Create source files
  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path source_move.txt -Value "moveable_file"
      New-Item -ItemType Directory -Path move_dir -Force | Out-Null
      Set-Content -Path move_dir/content.txt -Value "dir_content"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "moveable_file" > source_move.txt
      mkdir -p move_dir
      echo "dir_content" > move_dir/content.txt
    ]])
  end

  -- Move file
  envy.move("source_move.txt", "moved_file.txt")

  -- Move directory
  envy.move("move_dir", "moved_dir")

  -- Verify moves (source should not exist, dest should exist)
  if envy.PLATFORM == "windows" then
    envy.run([[
      if (Test-Path source_move.txt) {{ exit 1 }}
      if (-not (Test-Path moved_file.txt)) {{ exit 1 }}
      if (Test-Path move_dir) {{ exit 1 }}
      if (-not (Test-Path moved_dir/content.txt)) {{ exit 1 }}
      Write-Output "Move operations successful"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      test ! -f source_move.txt || exit 1
      test -f moved_file.txt || exit 1
      test ! -d move_dir || exit 1
      test -f moved_dir/content.txt || exit 1
      echo "Move operations successful"
    ]])
  end
end
""",
    "build_with_extract.lua": """-- Test build phase: envy.extract() to extract archive during build
IDENTITY = "local.build_with_extract@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

-- Skip stage phase, extract manually in build
STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  -- Don't extract yet, just prepare
  if envy.PLATFORM == "windows" then
    envy.run([[New-Item -ItemType Directory -Path manual_build -Force | Out-Null]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run("mkdir -p manual_build")
  end
end

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Testing envy.extract()")

  -- Extract the archive from fetch_dir into stage_dir
  local files_extracted = envy.extract(fetch_dir .. "/test.tar.gz", stage_dir)
  print("Extracted " .. files_extracted .. " files")

  -- Extract again with strip_components
  if envy.PLATFORM == "windows" then
    envy.run([[New-Item -ItemType Directory -Path stripped -Force | Out-Null]], {{ shell = ENVY_SHELL.POWERSHELL }})
    envy.run([[Set-Location stripped; $true]], {{ shell = ENVY_SHELL.POWERSHELL }})  -- Create directory
    envy.run([[New-Item -ItemType Directory -Path extracted_stripped -Force | Out-Null]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run("mkdir -p stripped")
    envy.run("cd stripped && true")  -- Create directory
    envy.run([[mkdir -p extracted_stripped]])
  end

  -- Note: extract extracts to cwd, so we need to work around this
  -- For now, just verify the first extraction worked
  if envy.PLATFORM == "windows" then
    envy.run([[
      if (-not (Test-Path root -PathType Container)) {{ exit 1 }}
      if (-not (Test-Path root/file1.txt)) {{ exit 1 }}
      Write-Output "Extract successful"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      test -d root || exit 1
      test -f root/file1.txt || exit 1
      echo "Extract successful"
    ]])
  end
end
""",
    "build_multiple_operations.lua": """-- Test build phase: multiple operations in sequence
IDENTITY = "local.build_multiple_operations@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Testing multiple operations")

  -- Operation 1: Create initial structure
  if envy.PLATFORM == "windows" then
    envy.run([[
      New-Item -ItemType Directory -Path step1 -Force | Out-Null
      Set-Content -Path step1/data.txt -Value "step1_output"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      mkdir -p step1
      echo "step1_output" > step1/data.txt
    ]])
  end

  -- Operation 2: Copy to next stage
  envy.copy("step1", "step2")

  -- Operation 3: Modify in step2
  if envy.PLATFORM == "windows" then
    envy.run([[
      Add-Content -Path step2/data.txt -Value "step2_additional"
      Set-Content -Path step2/new.txt -Value "step2_new"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "step2_additional" >> step2/data.txt
      echo "step2_new" > step2/new.txt
    ]])
  end

  -- Operation 4: Move to final location
  envy.move("step2", "final")

  -- Operation 5: Verify final state
  if envy.PLATFORM == "windows" then
    envy.run([[
      if (-not (Test-Path final -PathType Container)) {{
        Write-Output "missing final"
        exit 1
      }}
      if (Test-Path step2) {{
        Write-Output "step2 still exists"
        exit 1
      }}
      if (-not (Select-String -Path final/data.txt -Pattern "step1_output" -Quiet)) {{
        Write-Output "missing step1_output"
        exit 1
      }}
      if (-not (Select-String -Path final/data.txt -Pattern "step2_additional" -Quiet)) {{
        Write-Output "missing step2_additional"
        exit 1
      }}
      if (-not (Test-Path final/new.txt)) {{
        Write-Output "missing new.txt"
        exit 1
      }}
      Write-Output "All operations completed"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      test -d final || exit 1
      test ! -d step2 || exit 1
      grep -q step1_output final/data.txt || exit 1
      grep -q step2_additional final/data.txt || exit 1
      test -f final/new.txt || exit 1
      echo "All operations completed"
    ]])
  end

  print("Multiple operations successful")
end
""",
    "build_with_env.lua": """-- Test build phase: envy.run() with custom environment variables
IDENTITY = "local.build_with_env@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Testing custom environment variables")

  -- Run with custom environment
  local result
  if envy.PLATFORM == "windows" then
    result = envy.run([[
      Write-Output "BUILD_MODE=$env:BUILD_MODE"
      Write-Output "CUSTOM_VAR=$env:CUSTOM_VAR"
      if ($env:BUILD_MODE -ne "release") {{ exit 1 }}
      if ($env:CUSTOM_VAR -ne "test_value") {{ exit 1 }}
    ]], {{
      env = {{
        BUILD_MODE = "release",
        CUSTOM_VAR = "test_value"
      }},
      shell = ENVY_SHELL.POWERSHELL,
      capture = true
    }})
  else
    result = envy.run([[
      echo "BUILD_MODE=$BUILD_MODE"
      echo "CUSTOM_VAR=$CUSTOM_VAR"
      test "$BUILD_MODE" = "release" || exit 1
      test "$CUSTOM_VAR" = "test_value" || exit 1
    ]], {{
      env = {{
        BUILD_MODE = "release",
        CUSTOM_VAR = "test_value"
      }},
      capture = true
    }})
  end

  -- Verify environment was set
  if not result.stdout:match("BUILD_MODE=release") then
    error("BUILD_MODE not set correctly")
  end

  if not result.stdout:match("CUSTOM_VAR=test_value") then
    error("CUSTOM_VAR not set correctly")
  end

  print("Environment variables work correctly")
end
""",
    "build_with_cwd.lua": """-- Test build phase: envy.run() with custom working directory
IDENTITY = "local.build_with_cwd@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Testing custom working directory")

  -- Create subdirectory
  if envy.PLATFORM == "windows" then
    envy.run([[New-Item -ItemType Directory -Path subdir -Force | Out-Null]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run("mkdir -p subdir")
  end

  -- Run in subdirectory (relative path)
  if envy.PLATFORM == "windows" then
    envy.run([[
      (Get-Location).Path | Out-File -FilePath current_dir.txt
      Set-Content -Path marker.txt -Value "In subdirectory"
    ]], {{cwd = "subdir", shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      pwd > current_dir.txt
      echo "In subdirectory" > marker.txt
    ]], {{cwd = "subdir"}})
  end

  -- Verify we ran in subdirectory
  if envy.PLATFORM == "windows" then
    envy.run([[
      if (-not (Test-Path subdir/marker.txt)) {{
        Write-Output "missing subdir/marker.txt"
        exit 1
      }}
      $content = Get-Content subdir/current_dir.txt -Raw
      if ($content -notmatch "(?i)subdir") {{
        Write-Output "current_dir does not contain subdir: $content"
        exit 1
      }}
      Write-Output "CWD subdir verified"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      test -f subdir/marker.txt || exit 1
      grep -q subdir subdir/current_dir.txt || exit 1
    ]])
  end

  -- Create nested structure
  if envy.PLATFORM == "windows" then
    envy.run([[New-Item -ItemType Directory -Path nested/deep/dir -Force | Out-Null]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run("mkdir -p nested/deep/dir")
  end

  -- Run in deeply nested directory
  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path deep_marker.txt -Value "deep"
    ]], {{cwd = "nested/deep/dir", shell = ENVY_SHELL.POWERSHELL}})
  else
    envy.run([[
      echo "deep" > deep_marker.txt
    ]], {{cwd = "nested/deep/dir"}})
  end

  -- Verify
  if envy.PLATFORM == "windows" then
    envy.run([[
      if (-not (Test-Path nested/deep/dir/deep_marker.txt)) {{
        Write-Output "missing deep_marker.txt"
        exit 1
      }}
      $deep = Get-Content nested/deep/dir/deep_marker.txt -Raw
      if ($deep -notmatch "deep") {{
        Write-Output "deep_marker.txt does not contain deep"
        exit 1
      }}
      Write-Output "CWD operations successful"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      test -f nested/deep/dir/deep_marker.txt || exit 1
      echo "CWD operations successful"
    ]])
  end

  print("Custom working directory works correctly")
end
""",
    "build_error_nonzero_exit.lua": """-- Test build phase: error on non-zero exit code
IDENTITY = "local.build_error_nonzero_exit@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Testing error handling")

  -- This should fail and abort the build
  if envy.PLATFORM == "windows" then
    envy.run("exit 42", {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run("exit 42")
  end

  -- This should never execute
  error("Should not reach here")
end
""",
    "build_string_error.lua": """-- Test build phase: shell script with error
IDENTITY = "local.build_string_error@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

-- This build script should fail
BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  if envy.PLATFORM == "windows" then
    local result = envy.run([[Write-Output "Starting build"; Write-Error "Intentional failure"; exit 7 ]], {{ shell = ENVY_SHELL.POWERSHELL }})
    error("Intentional failure after ctx.run")
  else
    envy.run([[
      set -e
      echo "Starting build"
      false
      echo "This should not execute"
    ]], {{ check = true }})
  end
end
""",
    "build_access_dirs.lua": """-- Test build phase: access to fetch_dir, stage_dir
IDENTITY = "local.build_access_dirs@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Testing directory access")
  print("fetch_dir: " .. fetch_dir)
  print("stage_dir: " .. stage_dir)

  -- Verify directories exist
  if envy.PLATFORM == "windows" then
    envy.run([[
      if (-not (Test-Path -LiteralPath ']] .. fetch_dir .. [[' -PathType Container)) {{ exit 42 }}
      if (-not (Test-Path -LiteralPath ']] .. stage_dir .. [[' -PathType Container)) {{ exit 43 }}
      Write-Output "All directories exist"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      test -d "]] .. fetch_dir .. [[" || exit 1
      test -d "]] .. stage_dir .. [[" || exit 1
      echo "All directories exist"
    ]])
  end

  -- Verify fetch_dir contains the archive
  if envy.PLATFORM == "windows" then
    envy.run([[
      if (-not (Test-Path -LiteralPath ']] .. fetch_dir .. [[/test.tar.gz')) {{ exit 45 }}
      Write-Output "Archive found in fetch_dir"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      test -f "]] .. fetch_dir .. [[/test.tar.gz" || exit 1
      echo "Archive found in fetch_dir"
    ]])
  end

  -- Create output in stage_dir for later verification
  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path build_marker.txt -Value "Built successfully"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      echo "Built successfully" > build_marker.txt
    ]])
  end

  print("Directory access successful")
end
""",
    "build_nested_dirs.lua": """-- Test build phase: create complex nested directory structure
IDENTITY = "local.build_nested_dirs@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Creating nested directory structure")

  -- Create complex directory hierarchy
  if envy.PLATFORM == "windows" then
    envy.run([[
      New-Item -ItemType Directory -Path output/bin -Force | Out-Null
      New-Item -ItemType Directory -Path output/lib/x86_64 -Force | Out-Null
      New-Item -ItemType Directory -Path output/include/subproject -Force | Out-Null
      New-Item -ItemType Directory -Path output/share/doc -Force | Out-Null
      Set-Content -Path output/bin/app -Value "binary"
      Set-Content -Path output/lib/x86_64/libapp.so -Value "library"
      Set-Content -Path output/include/app.h -Value "header"
      Set-Content -Path output/include/subproject/sub.h -Value "nested_header"
      Set-Content -Path output/share/doc/README.md -Value "documentation"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      mkdir -p output/bin
      mkdir -p output/lib/x86_64
      mkdir -p output/include/subproject
      mkdir -p output/share/doc
      echo "binary" > output/bin/app
      echo "library" > output/lib/x86_64/libapp.so
      echo "header" > output/include/app.h
      echo "nested_header" > output/include/subproject/sub.h
      echo "documentation" > output/share/doc/README.md
    ]])
  end

  -- Copy nested structure
  envy.copy("output", "copied_output")

  -- Verify all files exist in copied structure
  if envy.PLATFORM == "windows" then
    envy.run([[
      if (-not (Test-Path copied_output/bin/app)) {{
        Write-Output "missing bin/app"
        exit 1
      }}
      if (-not (Test-Path copied_output/lib/x86_64/libapp.so)) {{
        Write-Output "missing libapp.so"
        exit 1
      }}
      if (-not (Test-Path copied_output/include/app.h)) {{
        Write-Output "missing app.h"
        exit 1
      }}
      if (-not (Test-Path copied_output/include/subproject/sub.h)) {{
        Write-Output "missing sub.h"
        exit 1
      }}
      if (-not (Test-Path copied_output/share/doc/README.md)) {{
        Write-Output "missing README.md"
        exit 1
      }}
      Write-Output "Nested directory operations successful"
    ]], {{ shell = ENVY_SHELL.POWERSHELL }})
  else
    envy.run([[
      test -f copied_output/bin/app || exit 1
      test -f copied_output/lib/x86_64/libapp.so || exit 1
      test -f copied_output/include/app.h || exit 1
      test -f copied_output/include/subproject/sub.h || exit 1
      test -f copied_output/share/doc/README.md || exit 1
      echo "Nested directory operations successful"
    ]])
  end

  print("Nested directory handling works correctly")
end
""",
    "build_output_capture.lua": """-- Test build phase: verify envy.run() captures stdout/stderr
IDENTITY = "local.build_output_capture@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Testing output capture")

  -- Capture output from command
  local result
  if envy.PLATFORM == "windows" then
    result = envy.run(
        [[if (-not $PSVersionTable) {{ Write-Output "psversion-init" }}; Write-Output "line1"; if (-not ("line1")) {{ Write-Output "line1" }}; Write-Output "line2"; Write-Output "line3"; exit 0]],
        {{ shell = ENVY_SHELL.POWERSHELL, capture = true }})
  else
    result = envy.run(
        [[
      echo "line1"
      echo "line2"
      echo "line3"
    ]],
        {{ capture = true }})
  end

  -- Verify stdout contains all lines
  if not result.stdout:match("line1") then
    error("Missing line1 in stdout")
  end
  if not result.stdout:match("line2") then
    error("Missing line2 in stdout")
  end
  if not result.stdout:match("line3") then
    error("Missing line3 in stdout")
  end

  -- Test with special characters
  if envy.PLATFORM == "windows" then
      result = envy.run(
          [[Write-Output "Special: !@#$%^&*()"; Write-Output "Unicode: hello"; Write-Output "Quotes: 'single' \\"double\\""; exit 0]],
          {{ shell = ENVY_SHELL.POWERSHELL, capture = true }})
  else
    result = envy.run(
        [[
      echo "Special: !@#$%^&*()"
      echo "Unicode: hello"
      echo "Quotes: 'single' \\"double\\""
    ]],
        {{ capture = true }})
  end

  if not result.stdout:match("Special:") then
    error("Missing special characters in output")
  end

  print("Output capture works correctly")
end
""",
    "build_function_returns_string.lua": """-- Test build phase: build = function(ctx, opts) that returns a string to execute
IDENTITY = "local.build_function_returns_string@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("BUILD function executing, preparing to return script")

  -- Do some setup work first
  if envy.PLATFORM == "windows" then
    envy.run("mkdir setup_dir 2> nul", {{ shell = ENVY_SHELL.CMD }})
  else
    envy.run("mkdir -p setup_dir")
  end

  -- Return a script to be executed
  if envy.PLATFORM == "windows" then
    return [[
      New-Item -ItemType Directory -Force -Path output_from_returned_script | Out-Null
      Set-Content -Path output_from_returned_script\\marker.txt -Value "returned_script_artifact" -NoNewline
    ]]
  else
    return [[
      mkdir -p output_from_returned_script
      echo "returned_script_artifact" > output_from_returned_script/marker.txt
    ]]
  end
end
""",
    "build_failfast.lua": """-- Test build phase: multi-line BUILD string fails on first error (fail-fast)
-- This verifies that shell scripts stop execution when a command fails.
IDENTITY = "local.build_failfast@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{strip = 1}}

-- Multi-line shell script where second command fails.
-- The third command should NOT run due to fail-fast behavior.
BUILD = [[
echo "line1"
false
echo "line2_should_not_run" > failfast_marker.txt
]]
""",
}


class TestBuildPhase(unittest.TestCase):
    """Tests for build phase (compilation and processing workflows)."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-build-test-"))
        self.specs_dir = Path(tempfile.mkdtemp(prefix="envy-build-specs-"))
        self.envy_test = test_config.get_envy_executable()
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

        # Create test archive and get its hash
        self.archive_path = self.specs_dir / "test.tar.gz"
        self.archive_hash = create_test_archive(self.archive_path)

        # Write all inline specs to temp directory
        for name, content in SPECS.items():
            spec_content = content.format(
                ARCHIVE_PATH=self.archive_path.as_posix(),
                ARCHIVE_HASH=self.archive_hash,
                SPECS_DIR=self.specs_dir.as_posix(),
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
        # Find the platform-specific package subdirectory
        for subdir in pkgs_dir.iterdir():
            if subdir.is_dir():
                pkg_dir = subdir / "pkg"
                if pkg_dir.exists():
                    return pkg_dir
                return subdir
        return None

    def run_spec(self, spec_file, identity, should_succeed=True):
        """Helper to run a spec and return result."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                identity,
                str(self.specs_dir / spec_file),
            ],
            capture_output=True,
            text=True,
        )

        if should_succeed:
            self.assertEqual(
                result.returncode,
                0,
                f"Spec {spec_file} failed:\nstdout: {result.stdout}\nstderr: {result.stderr}",
            )
        else:
            self.assertNotEqual(
                result.returncode,
                0,
                f"Spec {spec_file} should have failed but succeeded",
            )

        return result

    def test_build_nil_skips_phase(self):
        """Spec with no build field should skip build phase."""
        self.run_spec("build_nil.lua", "local.build_nil@v1")

        # Files should still be present from stage
        pkg_path = self.get_pkg_path("local.build_nil@v1")
        self.assertIsNotNone(pkg_path)
        self.assertTrue((pkg_path / "file1.txt").exists())

    def test_build_string_executes_shell(self):
        """Spec with build = "string" executes shell script."""
        self.run_spec("build_string.lua", "local.build_string@v1")

        # Verify build artifacts were created
        pkg_path = self.get_pkg_path("local.build_string@v1")
        self.assertIsNotNone(pkg_path)
        self.assertTrue((pkg_path / "build_output").exists())
        self.assertTrue((pkg_path / "build_output" / "artifact.txt").exists())

        # Verify artifact content
        content = (pkg_path / "build_output" / "artifact.txt").read_text()
        self.assertEqual(content.strip(), "build_artifact")

    def test_build_function_with_ctx_run(self):
        """Spec with build = function(ctx) uses envy.run()."""
        self.run_spec("build_function.lua", "local.build_function@v1")

        # Verify build artifacts
        pkg_path = self.get_pkg_path("local.build_function@v1")
        self.assertIsNotNone(pkg_path)
        self.assertTrue((pkg_path / "build_output" / "result.txt").exists())

        content = (pkg_path / "build_output" / "result.txt").read_text()
        self.assertEqual(content.strip(), "function_artifact")

    def test_build_with_package_dependency(self):
        """Build phase can access dependencies via envy.package()."""
        # Build spec with dependency (engine will build dependency automatically)
        self.run_spec("build_with_package.lua", "local.build_with_package@v1")

        # Verify dependency was used
        pkg_path = self.get_pkg_path("local.build_with_package@v1")
        self.assertIsNotNone(pkg_path)
        self.assertTrue((pkg_path / "from_dependency.txt").exists())

        content = (pkg_path / "from_dependency.txt").read_text()
        self.assertEqual(content.strip(), "dependency_data")

    def test_build_with_copy_operations(self):
        """Build phase can copy files and directories with envy.copy()."""
        self.run_spec("build_with_copy.lua", "local.build_with_copy@v1")

        # Verify copies
        pkg_path = self.get_pkg_path("local.build_with_copy@v1")
        self.assertIsNotNone(pkg_path)

        # Verify file copy
        self.assertTrue((pkg_path / "dest_file.txt").exists())
        content = (pkg_path / "dest_file.txt").read_text()
        self.assertEqual(content.strip(), "source_file")

        # Verify directory copy
        self.assertTrue((pkg_path / "dest_dir").is_dir())
        self.assertTrue((pkg_path / "dest_dir" / "file1.txt").exists())
        self.assertTrue((pkg_path / "dest_dir" / "file2.txt").exists())

    def test_build_with_move_operations(self):
        """Build phase can move files and directories with envy.move()."""
        self.run_spec("build_with_move.lua", "local.build_with_move@v1")

        # Verify moves
        pkg_path = self.get_pkg_path("local.build_with_move@v1")
        self.assertIsNotNone(pkg_path)

        # Source should not exist
        self.assertFalse((pkg_path / "source_move.txt").exists())
        self.assertFalse((pkg_path / "move_dir").exists())

        # Destination should exist
        self.assertTrue((pkg_path / "moved_file.txt").exists())
        self.assertTrue((pkg_path / "moved_dir").is_dir())
        self.assertTrue((pkg_path / "moved_dir" / "content.txt").exists())

    def test_build_with_extract(self):
        """Build phase can extract archives with envy.extract()."""
        self.run_spec("build_with_extract.lua", "local.build_with_extract@v1")

        # Verify extraction
        pkg_path = self.get_pkg_path("local.build_with_extract@v1")
        self.assertIsNotNone(pkg_path)
        self.assertTrue((pkg_path / "root").exists())
        self.assertTrue((pkg_path / "root" / "file1.txt").exists())

    def test_build_multiple_operations(self):
        """Build phase can chain multiple operations."""
        self.run_spec(
            "build_multiple_operations.lua", "local.build_multiple_operations@v1"
        )

        # Verify final state
        pkg_path = self.get_pkg_path("local.build_multiple_operations@v1")
        self.assertIsNotNone(pkg_path)

        # Intermediate directories should be gone
        self.assertFalse((pkg_path / "step2").exists())

        # Final directory should exist with all content
        self.assertTrue((pkg_path / "final").is_dir())
        self.assertTrue((pkg_path / "final" / "data.txt").exists())
        self.assertTrue((pkg_path / "final" / "new.txt").exists())

        # Verify content has both modifications
        content = (pkg_path / "final" / "data.txt").read_text()
        self.assertIn("step1_output", content)
        self.assertIn("step2_additional", content)

    def test_build_with_custom_env(self):
        """Build phase can set custom environment variables."""
        self.run_spec("build_with_env.lua", "local.build_with_env@v1")
        # Success is verified by spec not throwing an error

    def test_build_with_custom_cwd(self):
        """Build phase can run commands in custom working directory."""
        self.run_spec("build_with_cwd.lua", "local.build_with_cwd@v1")

        # Verify files were created in correct locations
        pkg_path = self.get_pkg_path("local.build_with_cwd@v1")
        self.assertIsNotNone(pkg_path)
        self.assertTrue((pkg_path / "subdir" / "marker.txt").exists())
        self.assertTrue(
            (pkg_path / "nested" / "deep" / "dir" / "deep_marker.txt").exists()
        )

    def test_build_error_nonzero_exit(self):
        """Build phase fails on non-zero exit code."""
        self.run_spec(
            "build_error_nonzero_exit.lua",
            "local.build_error_nonzero_exit@v1",
            should_succeed=False,
        )
        # Spec should fail with non-zero exit

    def test_build_string_error(self):
        """Build phase fails when shell script returns non-zero."""
        self.run_spec(
            "build_string_error.lua",
            "local.build_string_error@v1",
            should_succeed=False,
        )
        # Spec should fail with non-zero exit

    def test_build_access_directories(self):
        """Build phase has access to fetch_dir, stage_dir, install_dir."""
        self.run_spec("build_access_dirs.lua", "local.build_access_dirs@v1")

        pkg_path = self.get_pkg_path("local.build_access_dirs@v1")
        self.assertIsNotNone(pkg_path)
        # Spec validates directory access internally

    def test_build_nested_directory_structure(self):
        """Build phase can create and manipulate nested directories."""
        self.run_spec("build_nested_dirs.lua", "local.build_nested_dirs@v1")

        # Verify nested structure
        pkg_path = self.get_pkg_path("local.build_nested_dirs@v1")
        self.assertIsNotNone(pkg_path)

        # Verify copied nested structure
        self.assertTrue((pkg_path / "copied_output" / "bin" / "app").exists())
        self.assertTrue(
            (pkg_path / "copied_output" / "lib" / "x86_64" / "libapp.so").exists()
        )
        self.assertTrue((pkg_path / "copied_output" / "include" / "app.h").exists())
        self.assertTrue(
            (pkg_path / "copied_output" / "include" / "subproject" / "sub.h").exists()
        )
        self.assertTrue(
            (pkg_path / "copied_output" / "share" / "doc" / "README.md").exists()
        )

    def test_build_output_capture(self):
        """Build phase captures stdout correctly."""
        self.run_spec("build_output_capture.lua", "local.build_output_capture@v1")
        # Success is verified by spec validating captured output

    def test_build_function_returns_string(self):
        """Build function can return a string that gets executed."""
        self.run_spec(
            "build_function_returns_string.lua",
            "local.build_function_returns_string@v1",
        )

        # Verify setup directory was created by function body
        pkg_path = self.get_pkg_path("local.build_function_returns_string@v1")
        self.assertIsNotNone(pkg_path)
        self.assertTrue((pkg_path / "setup_dir").exists())

        # Verify output from returned script was created
        self.assertTrue((pkg_path / "output_from_returned_script").exists())
        self.assertTrue(
            (pkg_path / "output_from_returned_script" / "marker.txt").exists()
        )

        content = (pkg_path / "output_from_returned_script" / "marker.txt").read_text()
        self.assertEqual(content.strip(), "returned_script_artifact")

    def test_cache_path_includes_platform_arch(self):
        """Verify cache variant directory includes platform-arch prefix, not empty."""
        self.run_spec("build_function.lua", "local.build_function@v1")

        # Find the variant subdirectory under the identity
        identity_dir = self.cache_root / "packages" / "local.build_function@v1"
        self.assertTrue(
            identity_dir.exists(), f"Identity dir should exist: {identity_dir}"
        )

        variant_dirs = [d for d in identity_dir.iterdir() if d.is_dir()]
        self.assertEqual(
            len(variant_dirs), 1, "Should have exactly one variant directory"
        )

        variant_name = variant_dirs[0].name
        # Verify format is {platform}-{arch}-blake3-{hash}, not --blake3-{hash}
        self.assertNotIn(
            variant_name.startswith("--blake3-"),
            [True],
            f"Variant dir should not start with '--blake3-' (missing platform/arch): {variant_name}",
        )

        # Verify it starts with a valid platform
        valid_platforms = ("darwin-", "linux-", "windows-")
        self.assertTrue(
            any(variant_name.startswith(p) for p in valid_platforms),
            f"Variant dir should start with platform prefix: {variant_name}",
        )

        # Verify it contains blake3 hash marker
        self.assertIn(
            "-blake3-",
            variant_name,
            f"Variant dir should contain '-blake3-': {variant_name}",
        )

    def test_build_failfast_stops_on_first_error(self):
        """Multi-line BUILD string stops on first error (fail-fast behavior).

        Tests that when a command fails in a multi-line shell script,
        subsequent commands are NOT executed.
        """
        # Spec should fail because 'false' returns non-zero
        self.run_spec(
            "build_failfast.lua",
            "local.build_failfast@v1",
            should_succeed=False,
        )

        # Verify that the marker file was NOT created.
        # If fail-fast works correctly, the third command never runs.
        identity_dir = self.cache_root / "packages" / "local.build_failfast@v1"
        marker_found = False
        if identity_dir.exists():
            for root, dirs, files in os.walk(identity_dir):
                if "failfast_marker.txt" in files:
                    marker_found = True
                    break

        self.assertFalse(
            marker_found,
            "failfast_marker.txt should NOT exist - fail-fast should have stopped execution",
        )


if __name__ == "__main__":
    unittest.main()

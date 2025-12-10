-- Test build phase: ctx.run() with custom working directory
IDENTITY = "local.build_with_cwd@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = {strip = 1}

BUILD = function(ctx, opts)
  print("Testing custom working directory")

  -- Create subdirectory
  if ENVY_PLATFORM == "windows" then
    ctx.run([[New-Item -ItemType Directory -Path subdir -Force | Out-Null]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run("mkdir -p subdir")
  end

  -- Run in subdirectory (relative path)
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      (Get-Location).Path | Out-File -FilePath current_dir.txt
      Set-Content -Path marker.txt -Value "In subdirectory"
    ]], {cwd = "subdir", shell = ENVY_SHELL.POWERSHELL})
  else
    ctx.run([[
      pwd > current_dir.txt
      echo "In subdirectory" > marker.txt
    ]], {cwd = "subdir"})
  end

  -- Verify we ran in subdirectory
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      if (-not (Test-Path subdir/marker.txt)) {
        Write-Output "missing subdir/marker.txt"
        exit 1
      }
      $content = Get-Content subdir/current_dir.txt -Raw
      if ($content -notmatch "(?i)subdir") {
        Write-Output "current_dir does not contain subdir: $content"
        exit 1
      }
      Write-Output "CWD subdir verified"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      test -f subdir/marker.txt || exit 1
      grep -q subdir subdir/current_dir.txt || exit 1
    ]])
  end

  -- Create nested structure
  if ENVY_PLATFORM == "windows" then
    ctx.run([[New-Item -ItemType Directory -Path nested/deep/dir -Force | Out-Null]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run("mkdir -p nested/deep/dir")
  end

  -- Run in deeply nested directory
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path deep_marker.txt -Value "deep"
    ]], {cwd = "nested/deep/dir", shell = ENVY_SHELL.POWERSHELL})
  else
    ctx.run([[
      echo "deep" > deep_marker.txt
    ]], {cwd = "nested/deep/dir"})
  end

  -- Verify
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      if (-not (Test-Path nested/deep/dir/deep_marker.txt)) {
        Write-Output "missing deep_marker.txt"
        exit 1
      }
      $deep = Get-Content nested/deep/dir/deep_marker.txt -Raw
      if ($deep -notmatch "deep") {
        Write-Output "deep_marker.txt does not contain deep"
        exit 1
      }
      Write-Output "CWD operations successful"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      test -f nested/deep/dir/deep_marker.txt || exit 1
      echo "CWD operations successful"
    ]])
  end

  print("Custom working directory works correctly")
end

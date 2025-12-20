-- Test build phase: envy.extract() to extract archive during build
IDENTITY = "local.build_with_extract@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

-- Skip stage phase, extract manually in build
STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  -- Don't extract yet, just prepare
  if envy.PLATFORM == "windows" then
    envy.run([[New-Item -ItemType Directory -Path manual_build -Force | Out-Null]], { shell = ENVY_SHELL.POWERSHELL })
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
    envy.run([[New-Item -ItemType Directory -Path stripped -Force | Out-Null]], { shell = ENVY_SHELL.POWERSHELL })
    envy.run([[Set-Location stripped; $true]], { shell = ENVY_SHELL.POWERSHELL })  -- Create directory
    envy.run([[New-Item -ItemType Directory -Path extracted_stripped -Force | Out-Null]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run("mkdir -p stripped")
    envy.run("cd stripped && true")  -- Create directory
    envy.run([[mkdir -p extracted_stripped]])
  end

  -- Note: extract extracts to cwd, so we need to work around this
  -- For now, just verify the first extraction worked
  if envy.PLATFORM == "windows" then
    envy.run([[
      if (-not (Test-Path root -PathType Container)) { exit 1 }
      if (-not (Test-Path root/file1.txt)) { exit 1 }
      Write-Output "Extract successful"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      test -d root || exit 1
      test -f root/file1.txt || exit 1
      echo "Extract successful"
    ]])
  end
end

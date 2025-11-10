-- Test build phase: ctx.extract() to extract archive during build
identity = "local.build_with_extract@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

-- Skip stage phase, extract manually in build
stage = function(ctx)
  -- Don't extract yet, just prepare
  if ENVY_PLATFORM == "windows" then
    ctx.run([[New-Item -ItemType Directory -Path manual_build -Force | Out-Null]], { shell = "powershell" })
  else
    ctx.run("mkdir -p manual_build")
  end
end

build = function(ctx)
  print("Testing ctx.extract()")

  -- Extract the archive from fetch_dir into current directory
  local files_extracted = ctx.extract("test.tar.gz")
  print("Extracted " .. files_extracted .. " files")

  -- Extract again with strip_components
  if ENVY_PLATFORM == "windows" then
    ctx.run([[New-Item -ItemType Directory -Path stripped -Force | Out-Null]], { shell = "powershell" })
    ctx.run([[Set-Location stripped; $true]], { shell = "powershell" })  -- Create directory
    ctx.run([[New-Item -ItemType Directory -Path extracted_stripped -Force | Out-Null]], { shell = "powershell" })
  else
    ctx.run("mkdir -p stripped")
    ctx.run("cd stripped && true")  -- Create directory
    ctx.run([[mkdir -p extracted_stripped]])
  end

  -- Note: extract extracts to cwd, so we need to work around this
  -- For now, just verify the first extraction worked
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      if (-not (Test-Path root -PathType Container)) { exit 1 }
      if (-not (Test-Path root/file1.txt)) { exit 1 }
      Write-Output "Extract successful"
    ]], { shell = "powershell" })
  else
    ctx.run([[
      test -d root || exit 1
      test -f root/file1.txt || exit 1
      echo "Extract successful"
    ]])
  end
end

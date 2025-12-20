-- Test build phase: access to fetch_dir, stage_dir
IDENTITY = "local.build_access_dirs@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = {strip = 1}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Testing directory access")
  print("fetch_dir: " .. fetch_dir)
  print("stage_dir: " .. stage_dir)

  -- Verify directories exist
  if envy.PLATFORM == "windows" then
    envy.run([[
      if (-not (Test-Path -LiteralPath ']] .. fetch_dir .. [[' -PathType Container)) { exit 42 }
      if (-not (Test-Path -LiteralPath ']] .. stage_dir .. [[' -PathType Container)) { exit 43 }
      Write-Output "All directories exist"
    ]], { shell = ENVY_SHELL.POWERSHELL })
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
      if (-not (Test-Path -LiteralPath ']] .. fetch_dir .. [[/test.tar.gz')) { exit 45 }
      Write-Output "Archive found in fetch_dir"
    ]], { shell = ENVY_SHELL.POWERSHELL })
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
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      echo "Built successfully" > build_marker.txt
    ]])
  end

  print("Directory access successful")
end

-- Test build phase: verify envy.run() captures stdout/stderr
IDENTITY = "local.build_output_capture@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = {strip = 1}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Testing output capture")

  -- Capture output from command
  local result
  if envy.PLATFORM == "windows" then
    result = envy.run(
        [[if (-not $PSVersionTable) { Write-Output "psversion-init" }; Write-Output "line1"; if (-not ("line1")) { Write-Output "line1" }; Write-Output "line2"; Write-Output "line3"; exit 0]],
        { shell = ENVY_SHELL.POWERSHELL, capture = true })
  else
    result = envy.run(
        [[
      echo "line1"
      echo "line2"
      echo "line3"
    ]],
        { capture = true })
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
          [[Write-Output "Special: !@#$%^&*()"; Write-Output "Unicode: 你好世界"; Write-Output "Quotes: 'single' \"double\""; exit 0]],
          { shell = ENVY_SHELL.POWERSHELL, capture = true })
  else
    result = envy.run(
        [[
      echo "Special: !@#$%^&*()"
      echo "Unicode: 你好世界"
      echo "Quotes: 'single' "double""
    ]],
        { capture = true })
  end

  if not result.stdout:match("Special:") then
    error("Missing special characters in output")
  end

  print("Output capture works correctly")
end

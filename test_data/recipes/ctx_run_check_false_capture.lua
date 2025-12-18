-- Test ctx.run() with check=false and capture returns exit_code and output
IDENTITY = "local.ctx_run_check_false_capture@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})

  local res
  if envy.PLATFORM == "windows" then
    res = ctx.run([[
      Write-Output "stdout content"
      [Console]::Error.WriteLine("stderr content")
      exit 42
    ]], { shell = ENVY_SHELL.POWERSHELL, check = false, capture = true })
  else
    res = ctx.run([[
      echo "stdout content"
      echo "stderr content" >&2
      exit 42
    ]], { check = false, capture = true })
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
    ctx.run([[
      Set-Content -Path capture_success.txt -Value "Captured output successfully"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      echo "Captured output successfully" > capture_success.txt
    ]])
  end
end

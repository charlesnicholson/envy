-- Test ctx.run() with absolute cwd path
identity = "local.ctx_run_cwd_absolute@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    local temp = os.getenv("TEMP") or "C:\\\\Temp"
    local needs_sep = temp:match("[/\\\\]$") == nil
    local target = temp .. (needs_sep and "\\\\" or "") .. "envy_ctx_run_test.txt"
    ctx.run(string.format([[
      Set-Content -Path pwd_absolute.txt -Value (Get-Location).Path
      # Use Out-File with -Force to ensure file is created and flushed
      "Running in TEMP" | Out-File -FilePath "%s" -Force -Encoding ascii
      # Verify file was created
      if (-Not (Test-Path "%s")) {
        throw "Failed to create test file"
      }
    ]], target, target), {cwd = temp, shell = ENVY_SHELL.POWERSHELL})
  else
    ctx.run([[
      pwd > pwd_absolute.txt
      echo "Running in /tmp" > /tmp/envy_ctx_run_test.txt
      # Verify file was created
      test -f /tmp/envy_ctx_run_test.txt || exit 1
    ]], {cwd = "/tmp"})
  end
end

-- Test ctx.run() executes in stage_dir by default
IDENTITY = "local.ctx_run_in_stage_dir@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path pwd_default.txt -Value (Get-Location).Path
      Get-ChildItem | Select-Object -ExpandProperty Name | Set-Content -Path ls_output.txt
      if (Test-Path file1.txt) {
        Set-Content -Path stage_verification.txt -Value "Found file1.txt from archive"
      }
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      pwd > pwd_default.txt
      ls > ls_output.txt
      test -f file1.txt && echo "Found file1.txt from archive" > stage_verification.txt
    ]])
  end
end

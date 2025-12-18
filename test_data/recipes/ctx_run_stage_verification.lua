-- Test ctx.run() in stage for verification checks
IDENTITY = "local.ctx_run_stage_verification@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if envy.PLATFORM == "windows" then
    ctx.run([[
      if (-not (Test-Path file1.txt)) { throw "Missing file1.txt" }
      if ((Get-Item file1.txt).Length -eq 0) { throw "File is empty" }
      Set-Content -Path verification.txt -Value "All verification checks passed"
      if (-not (Test-Path verification.txt)) { throw "verification.txt missing post write" }
      exit 0
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      test -f file1.txt || (echo "Missing file1.txt" && exit 1)
      test -s file1.txt || (echo "File is empty" && exit 1)
      echo "All verification checks passed" > verification.txt
    ]])
  end
end

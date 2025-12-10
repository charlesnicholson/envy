-- Test ctx.run() error on non-zero exit code
IDENTITY = "local.ctx_run_error_nonzero@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Write-Output "About to fail"
      Set-Content -Path will_fail.txt -Value "Intentional failure sentinel"
      exit 42
    ]], { shell = ENVY_SHELL.POWERSHELL, check = true })
  else
    ctx.run([[
      echo "About to fail"
      exit 42
    ]], { check = true })
  end
end

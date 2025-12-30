-- Test envy.run() error on non-zero exit code
IDENTITY = "local.ctx_run_error_nonzero@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Write-Output "About to fail"
      Set-Content -Path will_fail.txt -Value "Intentional failure sentinel"
      exit 42
    ]], { shell = ENVY_SHELL.POWERSHELL, check = true })
  else
    envy.run([[
      echo "About to fail"
      exit 42
    ]], { check = true })
  end
end

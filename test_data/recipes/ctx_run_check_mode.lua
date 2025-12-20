-- Test envy.run() check mode catches failures
IDENTITY = "local.ctx_run_check_mode@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      cmd /c exit /b 7
      if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
      Write-Output "This should not execute"
    ]], { shell = ENVY_SHELL.POWERSHELL, check = true })
  else
    envy.run([[
      set -euo pipefail
      false
      echo "This should not execute"
    ]], { check = true })
  end
end

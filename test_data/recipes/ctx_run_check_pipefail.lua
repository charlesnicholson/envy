-- Test ctx.run() check mode catches pipe failures
IDENTITY = "local.ctx_run_check_pipefail@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if envy.PLATFORM == "windows" then
    ctx.run([[
      cmd /c "echo Start | cmd /c exit /b 3"
      if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    ]], { shell = ENVY_SHELL.POWERSHELL, check = true })
  else
    ctx.run([[
      set -euo pipefail
      echo "Start" | false | cat > should_fail.txt
    ]], { check = true })
  end
end

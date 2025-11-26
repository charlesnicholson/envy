-- Test ctx.run() strict mode catches pipe failures
identity = "local.ctx_run_strict_pipefail@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    -- Simulate pipe failure and exit non-zero explicitly.
    ctx.run([[
      cmd /c "echo Start | cmd /c exit /b 3"
      if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      set -euo pipefail
      echo "Start" | false | cat > should_fail.txt
    ]])
  end
end

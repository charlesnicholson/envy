-- Test ctx.run() error on signal termination
IDENTITY = "local.ctx_run_signal_term@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Stop-Process -Id $PID -Force
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      kill -TERM $$
    ]])
  end
end

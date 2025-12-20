-- Test envy.run() error on signal termination
IDENTITY = "local.ctx_run_signal_term@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Stop-Process -Id $PID -Force
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      kill -TERM $$
    ]])
  end
end

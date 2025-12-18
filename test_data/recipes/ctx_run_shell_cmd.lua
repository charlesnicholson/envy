-- Verify ctx.run() with shell="cmd" on Windows hosts
IDENTITY = "local.ctx_run_shell_cmd@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  if envy.PLATFORM ~= "windows" then
    error("ctx_run_shell_cmd should only run on Windows")
  end

  -- Use clean multi-line script without stray backslashes; ensure proper file creation.
  ctx.run([[
@echo off
setlocal enabledelayedexpansion
echo shell=cmd>shell_cmd_marker.txt
  ]], { shell = ENVY_SHELL.CMD })
end

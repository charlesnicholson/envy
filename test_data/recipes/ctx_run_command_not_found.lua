-- Test ctx.run() error when command not found
identity = "local.ctx_run_command_not_found@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      cmd /c nonexistent_command_xyz123
      exit $LASTEXITCODE
    ]], { shell = ENVY_SHELL.POWERSHELL, check = true })
  else
    ctx.run([[
      nonexistent_command_xyz123
    ]], { check = true })
  end
end

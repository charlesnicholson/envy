-- Test envy.run() error when command not found
IDENTITY = "local.ctx_run_command_not_found@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      cmd /c nonexistent_command_xyz123
      exit $LASTEXITCODE
    ]], { shell = ENVY_SHELL.POWERSHELL, check = true })
  else
    envy.run([[
      nonexistent_command_xyz123
    ]], { check = true })
  end
end

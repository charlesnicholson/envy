-- Test envy.run() error when cwd doesn't exist
IDENTITY = "local.ctx_run_invalid_cwd@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  local invalid_cwd
  local script
  if envy.PLATFORM == "windows" then
    invalid_cwd = "Z:/nonexistent/directory/that/does/not/exist"
    script = [[
      Write-Output "Should not execute"
    ]]
    envy.run(script, {cwd = invalid_cwd, shell = ENVY_SHELL.POWERSHELL, check = true})
  else
    invalid_cwd = "/nonexistent/directory/that/does/not/exist"
    envy.run([[
      echo "Should not execute"
    ]], {cwd = invalid_cwd, check = true})
  end
end

-- Test envy.run() continues after a failing command
IDENTITY = "local.ctx_run_continue_after_failure@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  -- Script should keep running even if an intermediate command fails
  if envy.PLATFORM == "windows" then
    envy.run([[
      cmd /c exit 1
      Set-Content -Path continued.txt -Value "This executes even after false"
    ]], {shell = ENVY_SHELL.POWERSHELL})
  else
    envy.run([[
      false || true
      echo "This executes even after false" > continued.txt
    ]])
  end
end

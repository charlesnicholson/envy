-- Intentional shell script failure for testing on all platforms
IDENTITY = "local.stage_shell_script_failure@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  if envy.PLATFORM == "windows" then
    envy.run([[Write-Output "About to fail"; exit 9; Write-Output "Should not reach"]], { shell = ENVY_SHELL.POWERSHELL, check = true })
  else
    envy.run([[
      set -e
      echo "About to fail"
      false
      echo "Should not reach"
    ]], { check = true })
  end
end
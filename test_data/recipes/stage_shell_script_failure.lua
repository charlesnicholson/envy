-- Intentional shell script failure for testing on all platforms
identity = "local.stage_shell_script_failure@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  if ENVY_PLATFORM == "windows" then
    ctx.run([[Write-Output "About to fail"; exit 9; Write-Output "Should not reach"]], { shell = "powershell" })
  else
    ctx.run([[echo "About to fail"; false; echo "Should not reach"]])
  end
end
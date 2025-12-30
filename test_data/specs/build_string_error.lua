-- Test build phase: shell script with error
IDENTITY = "local.build_string_error@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = {strip = 1}

-- This build script should fail
BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  if envy.PLATFORM == "windows" then
    local result = envy.run([[Write-Output "Starting build"; Write-Error "Intentional failure"; exit 7 ]], { shell = ENVY_SHELL.POWERSHELL })
    error("Intentional failure after ctx.run")
  else
    envy.run([[
      set -e
      echo "Starting build"
      false
      echo "This should not execute"
    ]], { check = true })
  end
end

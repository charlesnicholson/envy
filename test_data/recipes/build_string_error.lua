-- Test build phase: shell script with error
IDENTITY = "local.build_string_error@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = {strip = 1}

-- This build script should fail
BUILD = function(ctx, opts)
  if ENVY_PLATFORM == "windows" then
    local result = ctx.run([[Write-Output "Starting build"; Write-Error "Intentional failure"; exit 7 ]], { shell = ENVY_SHELL.POWERSHELL })
    error("Intentional failure after ctx.run")
  else
    ctx.run([[
      set -e
      echo "Starting build"
      false
      echo "This should not execute"
    ]], { check = true })
  end
end

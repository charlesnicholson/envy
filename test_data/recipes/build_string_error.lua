-- Test build phase: shell script with error
identity = "local.build_string_error@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {strip = 1}

-- This build script should fail
build = function(ctx)
  if ENVY_PLATFORM == "windows" then
    local result = ctx.run([[Write-Output "Starting build"; Write-Error "Intentional failure"; exit 7 ]], { shell = ENVY_SHELL.POWERSHELL })
    -- Force recipe failure regardless of ctx.run non-zero handling mechanics
    error("Intentional failure after ctx.run")
  else
    ctx.run([[
      set -e
      echo "Starting build"
      false
      echo "This should not execute"
    ]])
  end
end

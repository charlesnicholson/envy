-- Test build phase: ctx.run() with custom environment variables
IDENTITY = "local.build_with_env@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = {strip = 1}

BUILD = function(ctx, opts)
  print("Testing custom environment variables")

  -- Run with custom environment
  local result
  if envy.PLATFORM == "windows" then
    result = ctx.run([[
      Write-Output "BUILD_MODE=$env:BUILD_MODE"
      Write-Output "CUSTOM_VAR=$env:CUSTOM_VAR"
      if ($env:BUILD_MODE -ne "release") { exit 1 }
      if ($env:CUSTOM_VAR -ne "test_value") { exit 1 }
    ]], {
      env = {
        BUILD_MODE = "release",
        CUSTOM_VAR = "test_value"
      },
      shell = ENVY_SHELL.POWERSHELL,
      capture = true
    })
  else
    result = ctx.run([[
      echo "BUILD_MODE=$BUILD_MODE"
      echo "CUSTOM_VAR=$CUSTOM_VAR"
      test "$BUILD_MODE" = "release" || exit 1
      test "$CUSTOM_VAR" = "test_value" || exit 1
    ]], {
      env = {
        BUILD_MODE = "release",
        CUSTOM_VAR = "test_value"
      },
      capture = true
    })
  end

  -- Verify environment was set
  if not result.stdout:match("BUILD_MODE=release") then
    error("BUILD_MODE not set correctly")
  end

  if not result.stdout:match("CUSTOM_VAR=test_value") then
    error("CUSTOM_VAR not set correctly")
  end

  print("Environment variables work correctly")
end

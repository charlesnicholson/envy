-- Test build phase: build = function(ctx) (programmatic with ctx.run())
identity = "local.build_function@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {strip = 1}

build = function(ctx)
  print("Building with ctx.run()")

  -- Create build artifacts
  local result
  if ENVY_PLATFORM == "windows" then
    result = ctx.run([[
      New-Item -ItemType Directory -Path build_output -Force | Out-Null
      Set-Content -Path build_output/result.txt -Value "function_artifact"
      Write-Output "Build complete"
    ]], { shell = "powershell" })
  else
    result = ctx.run([[
      mkdir -p build_output
      echo "function_artifact" > build_output/result.txt
      echo "Build complete"
    ]])
  end

  -- Verify stdout contains expected output
  if not result.stdout:match("Build complete") then
    error("Expected 'Build complete' in stdout")
  end

  print("Build finished successfully")
end

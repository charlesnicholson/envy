-- Test build phase: build = function(ctx, opts) (programmatic with envy.run())
IDENTITY = "local.build_function@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = {strip = 1}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Building with envy.run()")

  -- Create build artifacts
  local result
  if envy.PLATFORM == "windows" then
    result = envy.run([[mkdir build_output 2> nul & echo function_artifact > build_output\result.txt & if not exist build_output\result.txt ( echo Artifact missing & exit /b 1 ) & echo Build complete & exit /b 0 ]],
                     { shell = ENVY_SHELL.CMD, capture = true })
  else
    result = envy.run([[
      mkdir -p build_output
      echo "function_artifact" > build_output/result.txt
      echo "Build complete"
    ]],
                     { capture = true })
  end

  -- Verify stdout contains expected output
  if not result.stdout:match("Build complete") then
    error("Expected 'Build complete' in stdout")
  end

  print("Build finished successfully")
end

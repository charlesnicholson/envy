-- Test build phase: error on non-zero exit code
IDENTITY = "local.build_error_nonzero_exit@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = {strip = 1}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Testing error handling")

  -- This should fail and abort the build
  if envy.PLATFORM == "windows" then
    envy.run("exit 42", { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run("exit 42")
  end

  -- This should never execute
  error("Should not reach here")
end

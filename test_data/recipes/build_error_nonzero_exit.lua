-- Test build phase: error on non-zero exit code
identity = "local.build_error_nonzero_exit@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {strip = 1}

build = function(ctx)
  print("Testing error handling")

  -- This should fail and abort the build
  if ENVY_PLATFORM == "windows" then
    ctx.run("exit 42", { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run("exit 42")
  end

  -- This should never execute
  error("Should not reach here")
end

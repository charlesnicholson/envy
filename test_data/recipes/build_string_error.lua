-- Test build phase: shell script with error
identity = "local.build_string_error@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {strip = 1}

-- This build script should fail
build = [[
  set -e
  echo "Starting build"
  false
  echo "This should not execute"
]]

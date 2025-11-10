-- Test build phase: build = "shell script" (shell execution)
identity = "local.build_string@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {strip = 1}

build = [[
  echo "Building in shell script mode"
  mkdir -p build_output
  echo "build_artifact" > build_output/artifact.txt
  ls -la
]]

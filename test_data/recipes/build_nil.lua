-- Test build phase: build = nil (skip build)
identity = "local.build_nil@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {strip = 1}

-- No build field - should skip build phase and proceed to install

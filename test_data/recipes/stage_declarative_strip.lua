-- Test declarative stage with strip option
identity = "local.stage_declarative_strip@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {
  strip = 1  -- Remove root/ top-level directory
}

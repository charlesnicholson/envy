-- Test default stage phase (no stage field, auto-extract archives)
IDENTITY = "local.stage_default@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

-- No stage field - should auto-extract to install_dir

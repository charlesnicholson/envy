-- Test default stage phase (no stage field, auto-extract archives)
identity = "local.stage_default@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef917a6d5daf41890d08b9cfa97b0d2161cab897635cc617c195fcdc4df1730c"
}

-- No stage field - should auto-extract to install_dir

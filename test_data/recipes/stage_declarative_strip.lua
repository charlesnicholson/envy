-- Test declarative stage with strip option
identity = "local.stage_declarative_strip@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef917a6d5daf41890d08b9cfa97b0d2161cab897635cc617c195fcdc4df1730c"
}

stage = {
  strip = 1  -- Remove root/ top-level directory
}

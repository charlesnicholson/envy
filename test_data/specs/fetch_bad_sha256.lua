-- Test declarative fetch with wrong SHA256 (should fail verification)
IDENTITY = "local.fetch_bad_sha256@v1"

-- Wrong sha256 - should fail after download
FETCH = {
  source = "test_data/lua/simple.lua",
  sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
}

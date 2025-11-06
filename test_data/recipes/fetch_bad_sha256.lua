-- Test declarative fetch with wrong SHA256 (should fail verification)
identity = "local.fetch_bad_sha256@v1"

-- Wrong sha256 - should fail after download
fetch = {
  url = "test_data/lua/simple.lua",
  sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
}

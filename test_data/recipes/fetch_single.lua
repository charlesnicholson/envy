-- Test declarative fetch with single table format and SHA256 verification
identity = "local.fetch_single@v1"

-- Single table format with optional sha256
fetch = {
  url = "test_data/lua/simple.lua",
  sha256 = "5d414f661043721a92d3f39539baae9decb27fddf706d592d4a9431182eb87a9"
}

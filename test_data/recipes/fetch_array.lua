-- Test declarative fetch with array format (concurrent downloads)
identity = "local.fetch_array@v1"

-- Array format: multiple files with optional sha256
fetch = {
  {
    source = "test_data/lua/simple.lua",
    sha256 = "5d414f661043721a92d3f39539baae9decb27fddf706d592d4a9431182eb87a9"
  },
  {
    source = "test_data/lua/print_single.lua",
    sha256 = "b80792336156c7b0f7fe02eeef24610d2d52a10d1810397744471d1dc5738180"
  },
  {
    source = "test_data/lua/print_multiple.lua"
    -- No sha256 - should still work (permissive mode)
  }
}

-- Test per-file caching across partial failures
-- Two files succeed, one fails, then completion reuses cached files
IDENTITY = "local.fetch_partial@v1"

FETCH = {
  {
    source = "test_data/lua/simple.lua",
    sha256 = "5d414f661043721a92d3f39539baae9decb27fddf706d592d4a9431182eb87a9"
  },
  {
    source = "test_data/lua/print_single.lua",
    sha256 = "b80792336156c7b0f7fe02eeef24610d2d52a10d1810397744471d1dc5738180"
  },
  {
    -- This file will be created by the test after first run
    source = "file://__TEMP__/fetch_partial_missing.lua",
    sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
  }
}

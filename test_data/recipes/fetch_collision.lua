-- Test declarative fetch with filename collision (should error)
identity = "local.fetch_collision@v1"

-- Both URLs have the same basename "simple.lua" - should error
fetch = {
  { url = "test_data/lua/simple.lua" },
  { url = "test_data/recipes/simple.lua" }  -- Different file, same basename
}

-- Test declarative fetch with filename collision (should error)
identity = "local.fetch_collision@v1"

-- Both URLs have the same basename "simple.lua" - should error
fetch = {
  { source = "test_data/lua/simple.lua" },
  { source = "test_data/recipes/simple.lua" }  -- Different file, same basename
}

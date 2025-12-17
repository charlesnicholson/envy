-- Consumer with multiple weak dependencies for hash testing
IDENTITY = "local.hash_consumer_multi@v1"

DEPENDENCIES = {
  {
    product = "zzz_tool",  -- Sorts last alphabetically
    weak = {
      recipe = "local.hash_provider_zzz@v1",
      source = "test_data/recipes/hash_provider_zzz.lua",
    },
  },
  {
    product = "aaa_tool",  -- Sorts first alphabetically
    weak = {
      recipe = "local.hash_provider_aaa@v1",
      source = "test_data/recipes/hash_provider_aaa.lua",
    },
  },
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

INSTALL = function(ctx)
end

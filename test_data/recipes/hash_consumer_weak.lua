-- Consumer with weak product dependency for hash testing
IDENTITY = "local.hash_consumer_weak@v1"

-- Note: weak fallback source must be absolute or relative to THIS recipe's location
-- Since tests run from project root, we use relative path from test_data/recipes/
DEPENDENCIES = {
  {
    product = "tool",
    weak = {
      recipe = "local.hash_provider_a@v1",
      source = "hash_provider_a.lua",  -- Relative to this recipe's directory
    },
  },
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end

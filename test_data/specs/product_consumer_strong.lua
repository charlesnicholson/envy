-- Consumer with strong product dependency
IDENTITY = "local.product_consumer_strong@v1"

DEPENDENCIES = {
  {
    product = "tool",
    spec = "local.product_provider@v1",
    source = "product_provider.lua",
  },
}

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

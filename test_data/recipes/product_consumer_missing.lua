-- Consumer with missing product dependency (no fallback)
IDENTITY = "local.product_consumer_missing@v1"

DEPENDENCIES = {
  {
    product = "missing_tool",
  },
}

INSTALL = function(ctx)
end

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

-- Consumer with missing product dependency (no fallback)
identity = "local.product_consumer_missing@v1"

dependencies = {
  {
    product = "missing_tool",
  },
}

install = function(ctx)
  ctx.mark_install_complete()
end

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

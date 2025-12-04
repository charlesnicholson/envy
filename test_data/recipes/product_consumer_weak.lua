-- Consumer with weak product dependency (fallback)
identity = "local.product_consumer_weak@v1"

dependencies = {
  {
    product = "tool",
    recipe = "local.product_provider@v1",
    weak = {
      recipe = "local.product_provider@v1",
      source = "product_provider.lua",
    },
  },
}

install = function(ctx)
  ctx.mark_install_complete()
end

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

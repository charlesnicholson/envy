-- Second provider for collision testing
IDENTITY = "local.product_provider_b@v1"
PRODUCTS = { tool = "bin/other" }

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

INSTALL = function(ctx)
  ctx.mark_install_complete()
end

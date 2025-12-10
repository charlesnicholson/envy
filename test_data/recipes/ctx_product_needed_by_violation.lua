IDENTITY = "local.ctx_product_needed_by_violation@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

DEPENDENCIES = {
  {
    recipe = "local.product_provider@v1",
    source = "product_provider.lua",
    product = "tool",
    needed_by = "install",
  },
}

STAGE = function(ctx)
  ctx.product("tool")
end

INSTALL = function(ctx)
  ctx.mark_install_complete()
end

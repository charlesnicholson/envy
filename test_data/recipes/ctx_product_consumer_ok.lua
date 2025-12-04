identity = "local.ctx_product_consumer_ok@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

dependencies = {
  {
    recipe = "local.product_provider@v1",
    source = "product_provider.lua",
    product = "tool",
    needed_by = "stage",
  },
}

stage = function(ctx)
  local val = ctx.product("tool")
  assert(val:match("bin/tool"), "expected product path")
end

install = function(ctx)
  ctx.mark_install_complete()
end

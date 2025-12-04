identity = "local.ctx_product_missing_dep@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

stage = function(ctx)
  ctx.product("tool")
end

install = function(ctx)
  ctx.mark_install_complete()
end

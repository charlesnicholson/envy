IDENTITY = "local.ctx_asset_consumer_ok@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

DEPENDENCIES = {
  { recipe = "local.ctx_asset_provider@v1", source = "ctx_asset_provider.lua", needed_by = "stage" },
}

STAGE = function(ctx)
  local path = ctx.asset("local.ctx_asset_provider@v1")
  assert(path:match("ctx_asset_provider"), "asset path should include provider identity")
end

INSTALL = function(ctx)
  ctx.mark_install_complete()
end

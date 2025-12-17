IDENTITY = "local.ctx_asset_user_consumer@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

DEPENDENCIES = {
  { recipe = "local.ctx_asset_user_provider@v1", source = "ctx_asset_user_provider.lua", needed_by = "stage" },
}

STAGE = function(ctx)
  ctx.asset("local.ctx_asset_user_provider@v1")
end

INSTALL = function(ctx)
end

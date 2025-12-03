identity = "local.ctx_asset_user_consumer@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

dependencies = {
  { recipe = "local.ctx_asset_user_provider@v1", source = "ctx_asset_user_provider.lua", needed_by = "stage" },
}

stage = function(ctx)
  ctx.asset("local.ctx_asset_user_provider@v1")
end

install = function(ctx)
  ctx.mark_install_complete()
end

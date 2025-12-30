IDENTITY = "local.ctx_asset_needed_by_violation@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

DEPENDENCIES = {
  { spec = "local.ctx_asset_provider@v1", source = "ctx_asset_provider.lua", needed_by = "install" },
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  -- needed_by is install, so this should fail when enforced
  envy.asset("local.ctx_asset_provider@v1")
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end

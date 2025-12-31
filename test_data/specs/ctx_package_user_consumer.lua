IDENTITY = "local.ctx_package_user_consumer@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

DEPENDENCIES = {
  { spec = "local.ctx_package_user_provider@v1", source = "ctx_package_user_provider.lua", needed_by = "stage" },
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.package("local.ctx_package_user_provider@v1")
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end

IDENTITY = "local.ctx_package_consumer_ok@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

DEPENDENCIES = {
  { spec = "local.ctx_package_provider@v1", source = "ctx_package_provider.lua", needed_by = "stage" },
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  local path = envy.package("local.ctx_package_provider@v1")
  assert(path:match("ctx_package_provider"), "package path should include provider identity")
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end

IDENTITY = "local.ctx_package_missing_dep@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.package("local.nonexistent_dep@v1")
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end

IDENTITY = "local.ctx_package_provider@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end

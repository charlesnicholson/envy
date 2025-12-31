-- Product provider with cached package
IDENTITY = "local.product_provider@v1"
PRODUCTS = { tool = "bin/tool" }

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- No real payload needed; just mark complete to populate pkg_path
end

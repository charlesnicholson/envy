IDENTITY = "local.cycle_a@v1"
PRODUCTS = { tool_a = "bin/a" }

DEPENDENCIES = {
  {
    product = "tool_b",
    spec = "local.cycle_b@v1",
    weak = {
      spec = "local.cycle_b@v1",
      source = "product_cycle_b.lua",
    }
  },
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end

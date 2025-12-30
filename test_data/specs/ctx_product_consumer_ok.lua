IDENTITY = "local.ctx_product_consumer_ok@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

DEPENDENCIES = {
  {
    spec = "local.product_provider@v1",
    source = "product_provider.lua",
    product = "tool",
    needed_by = "stage",
  },
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  local val = envy.product("tool")
  assert(val:match("bin/tool"), "expected product path")
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end

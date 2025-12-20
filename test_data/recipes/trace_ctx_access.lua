-- Exercises ctx.asset and ctx.product access traces (allowed + denied)
IDENTITY = "local.trace_ctx_access@v1"

DEPENDENCIES = {
  { recipe = "local.dep_val_lib@v1", source = "dep_val_lib.lua", needed_by = "stage" },
  { product = "tool", recipe = "local.product_provider@v1", source = "product_provider.lua", needed_by = "stage" },
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all({ strip = 1 })

  -- Allowed asset access
  envy.asset("local.dep_val_lib@v1")

  -- Denied asset access (undeclared)
  local ok, err = pcall(function() envy.asset("local.missing@v1") end)
  assert(not ok, "expected missing asset access to fail")

  -- Allowed product access
  envy.product("tool")

  -- Denied product access (undeclared)
  local ok2, err2 = pcall(function() envy.product("missing_prod") end)
  assert(not ok2, "expected missing product access to fail")
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end

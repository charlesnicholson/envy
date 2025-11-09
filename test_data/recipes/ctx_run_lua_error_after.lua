-- Test Lua error after ctx.run() succeeds
identity = "local.ctx_run_lua_error_after@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- This should succeed
  ctx.run([[
    echo "Before Lua error" > before_lua_error.txt
  ]])

  -- Then Lua error
  error("Intentional Lua error after ctx.run")
end

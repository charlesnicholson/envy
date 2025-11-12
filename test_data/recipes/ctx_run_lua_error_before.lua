-- Test Lua error before ctx.run()
identity = "local.ctx_run_lua_error_before@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Lua error happens first
  error("Intentional Lua error before ctx.run")

  -- This should never execute
  ctx.run([[
    echo "After Lua error" > after_lua_error.txt
  ]])
end

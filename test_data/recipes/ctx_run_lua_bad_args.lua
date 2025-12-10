-- Test ctx.run() with invalid arguments (Lua error)
IDENTITY = "local.ctx_run_lua_bad_args@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})

  -- Call with invalid argument value (nil) to ensure failure consistently.
  ctx.run(nil)
end

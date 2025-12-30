-- Test envy.run() with invalid arguments (Lua error)
IDENTITY = "local.ctx_run_lua_bad_args@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  -- Call with invalid argument value (nil) to ensure failure consistently.
  envy.run(nil)
end

-- Test Lua error after envy.run() succeeds
IDENTITY = "local.ctx_run_lua_error_after@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path before_lua_error.txt -Value "Before Lua error"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      echo "Before Lua error" > before_lua_error.txt
    ]])
  end

  -- Then Lua error
  error("Intentional Lua error after ctx.run")
end

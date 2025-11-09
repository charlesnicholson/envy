-- Test ctx.run() inherits environment variables like PATH
identity = "local.ctx_run_env_inherit@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Verify PATH is inherited (allows us to run commands)
  ctx.run([[
    echo "PATH=$PATH" > inherited_path.txt
    which echo > which_echo.txt
    test -n "$PATH" && echo "PATH inherited" > path_verification.txt
  ]])
end

-- Test ctx.run() with custom environment variables
identity = "local.ctx_run_env_custom@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Run with custom environment variables
  ctx.run([[
    echo "MY_VAR=$MY_VAR" > env_output.txt
    echo "MY_NUM=$MY_NUM" >> env_output.txt
    echo "PATH_AVAILABLE=${PATH:+yes}" >> env_output.txt
  ]], {env = {MY_VAR = "test_value", MY_NUM = "42"}})
end

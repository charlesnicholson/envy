-- Test ctx.run() can override inherited environment variables
identity = "local.ctx_run_env_override@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Override a variable (USER is typically set)
  ctx.run([[
    echo "USER=$USER" > overridden_user.txt
  ]], {env = {USER = "test_override_user"}})
end

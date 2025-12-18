-- Test ctx.run() can override inherited environment variables
IDENTITY = "local.ctx_run_env_override@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})

  -- Override a variable (USER is typically set)
    if envy.PLATFORM == "windows" then
      ctx.run([[
        if ($env:USER -ne 'test_override_user') { exit 42 }
        Set-Content -Path overridden_user.txt -Value ("USER=" + $env:USER)
      ]], {env = {USER = "test_override_user"}, shell = ENVY_SHELL.POWERSHELL})
  else
    ctx.run([[
      echo "USER=$USER" > overridden_user.txt
    ]], {env = {USER = "test_override_user"}})
  end
end

-- Test ctx.run() with custom environment variables
identity = "local.ctx_run_env_custom@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  local env_values = {MY_VAR = "test_value", MY_NUM = "42"}

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path env_output.txt -Value ("MY_VAR=" + $env:MY_VAR)
      Add-Content -Path env_output.txt -Value ("MY_NUM=" + $env:MY_NUM)
      if ($env:PATH) { $status = "yes" } else { $status = "" }
      Add-Content -Path env_output.txt -Value ("PATH_AVAILABLE=" + $status)
    ]], {env = env_values, shell = "powershell"})
  else
    ctx.run([[
      echo "MY_VAR=$MY_VAR" > env_output.txt
      echo "MY_NUM=$MY_NUM" >> env_output.txt
      echo "PATH_AVAILABLE=${PATH:+yes}" >> env_output.txt
    ]], {env = env_values})
  end
end

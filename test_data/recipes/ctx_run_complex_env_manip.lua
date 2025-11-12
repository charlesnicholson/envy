-- Test ctx.run() with complex environment manipulation
identity = "local.ctx_run_complex_env_manip@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- First run with base environment
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path env_step1.txt -Value ("BASE_VAR=" + $env:BASE_VAR)
    ]], {env = {BASE_VAR = "base_value"}, shell = "powershell"})
  else
    ctx.run([[
      echo "BASE_VAR=$BASE_VAR" > env_step1.txt
    ]], {env = {BASE_VAR = "base_value"}})
  end

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path env_step2.txt -Value ("BASE_VAR=" + $env:BASE_VAR)
      Add-Content -Path env_step2.txt -Value ("EXTRA_VAR=" + $env:EXTRA_VAR)
      Add-Content -Path env_step2.txt -Value ("ANOTHER=" + $env:ANOTHER)
    ]], {env = {
      BASE_VAR = "modified",
      EXTRA_VAR = "extra",
      ANOTHER = "another"
    }, shell = "powershell"})
  else
    ctx.run([[
      echo "BASE_VAR=$BASE_VAR" > env_step2.txt
      echo "EXTRA_VAR=$EXTRA_VAR" >> env_step2.txt
      echo "ANOTHER=$ANOTHER" >> env_step2.txt
    ]], {env = {
      BASE_VAR = "modified",
      EXTRA_VAR = "extra",
      ANOTHER = "another"
    }})
  end

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path env_step3.txt -Value ("CONFIG_DIR=" + $env:CONFIG_DIR)
      Add-Content -Path env_step3.txt -Value ("DATA_DIR=" + $env:DATA_DIR)
      Add-Content -Path env_step3.txt -Value ("LOG_LEVEL=" + $env:LOG_LEVEL)
    ]], {env = {
      CONFIG_DIR = "C:/etc/myapp",
      DATA_DIR = "C:/var/lib/myapp",
      LOG_LEVEL = "debug"
    }, shell = "powershell"})
  else
    ctx.run([[
      echo "CONFIG_DIR=$CONFIG_DIR" > env_step3.txt
      echo "DATA_DIR=$DATA_DIR" >> env_step3.txt
      echo "LOG_LEVEL=$LOG_LEVEL" >> env_step3.txt
    ]], {env = {
      CONFIG_DIR = "/etc/myapp",
      DATA_DIR = "/var/lib/myapp",
      LOG_LEVEL = "debug"
    }})
  end
end

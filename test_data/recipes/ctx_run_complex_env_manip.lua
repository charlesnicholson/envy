-- Test ctx.run() with complex environment manipulation
identity = "local.ctx_run_complex_env_manip@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- First run with base environment
  ctx.run([[
    echo "BASE_VAR=$BASE_VAR" > env_step1.txt
  ]], {env = {BASE_VAR = "base_value"}})

  -- Second run with extended environment
  ctx.run([[
    echo "BASE_VAR=$BASE_VAR" > env_step2.txt
    echo "EXTRA_VAR=$EXTRA_VAR" >> env_step2.txt
    echo "ANOTHER=$ANOTHER" >> env_step2.txt
  ]], {env = {
    BASE_VAR = "modified",
    EXTRA_VAR = "extra",
    ANOTHER = "another"
  }})

  -- Third run with completely different environment
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

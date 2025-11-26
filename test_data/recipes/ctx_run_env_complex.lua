-- Test ctx.run() with complex environment variables
identity = "local.ctx_run_env_complex@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})

  local env_values = {
    STRING = "hello_world",
    NUMBER = "12345",
    WITH_SPACE = "value with spaces",
    SPECIAL = "a=b:c;d"
  }

    if ENVY_PLATFORM == "windows" then
      ctx.run([[
        if (-not $env:STRING) { exit 44 }
        Set-Content -Path env_complex.txt -Value ("STRING=" + $env:STRING)
        Add-Content -Path env_complex.txt -Value ("NUMBER=" + $env:NUMBER)
        Add-Content -Path env_complex.txt -Value ("WITH_SPACE=" + $env:WITH_SPACE)
        Add-Content -Path env_complex.txt -Value ("SPECIAL=" + $env:SPECIAL)
      ]], {env = env_values, shell = ENVY_SHELL.POWERSHELL})
  else
    ctx.run([[
      echo "STRING=$STRING" > env_complex.txt
      echo "NUMBER=$NUMBER" >> env_complex.txt
      echo "WITH_SPACE=$WITH_SPACE" >> env_complex.txt
      echo "SPECIAL=$SPECIAL" >> env_complex.txt
    ]], {env = env_values})
  end
end

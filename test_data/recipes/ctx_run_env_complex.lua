-- Test ctx.run() with complex environment variables
identity = "local.ctx_run_env_complex@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Test various types of environment values
  ctx.run([[
    echo "STRING=$STRING" > env_complex.txt
    echo "NUMBER=$NUMBER" >> env_complex.txt
    echo "WITH_SPACE=$WITH_SPACE" >> env_complex.txt
    echo "SPECIAL=$SPECIAL" >> env_complex.txt
  ]], {env = {
    STRING = "hello_world",
    NUMBER = "12345",
    WITH_SPACE = "value with spaces",
    SPECIAL = "a=b:c;d"
  }})
end

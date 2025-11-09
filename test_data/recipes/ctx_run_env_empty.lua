-- Test ctx.run() with empty env table still inherits
identity = "local.ctx_run_env_empty@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Empty env table should not break anything
  ctx.run([[
    echo "Empty env table works" > empty_env.txt
    test -n "$PATH" && echo "PATH still available" >> empty_env.txt
  ]], {env = {}})
end

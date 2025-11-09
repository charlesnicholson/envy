-- Test ctx.run() with all options combined
identity = "local.ctx_run_all_options@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Create working directory
  ctx.run([[
    mkdir -p subdir
  ]])

  -- Use all options at once: cwd, env, disable_strict
  ctx.run([[
    pwd > all_opts_pwd.txt
    echo "MY_VAR=$MY_VAR" > all_opts_env.txt
    echo "ANOTHER=$ANOTHER" >> all_opts_env.txt

    # This would fail in strict mode but we're disabling it
    false
    echo "Continued after false" > all_opts_continued.txt
  ]], {
    cwd = "subdir",
    env = {MY_VAR = "test", ANOTHER = "value"},
    disable_strict = true
  })
end

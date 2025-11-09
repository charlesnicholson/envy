-- Test ctx.run() with various option combinations
identity = "local.ctx_run_option_combinations@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  ctx.run([[
    mkdir -p dir1 dir2
  ]])

  -- Combination 1: cwd + env
  ctx.run([[
    pwd > combo1_pwd.txt
    echo "VAR1=$VAR1" > combo1_env.txt
  ]], {cwd = "dir1", env = {VAR1 = "value1"}})

  -- Combination 2: cwd + disable_strict
  ctx.run([[
    pwd > combo2_pwd.txt
    false
    echo "After false" > combo2_continued.txt
  ]], {cwd = "dir2", disable_strict = true})

  -- Combination 3: env + disable_strict (default cwd)
  ctx.run([[
    echo "VAR2=$VAR2" > combo3_env.txt
    false
    echo "Continued" >> combo3_env.txt
  ]], {env = {VAR2 = "value2"}, disable_strict = true})

  -- Combination 4: Just env
  ctx.run([[
    echo "VAR3=$VAR3" > combo4_env.txt
  ]], {env = {VAR3 = "value3"}})

  -- Combination 5: Just cwd
  ctx.run([[
    pwd > combo5_pwd.txt
  ]], {cwd = "dir1"})

  -- Combination 6: Just disable_strict
  ctx.run([[
    false
    echo "Standalone disable_strict" > combo6_continued.txt
  ]], {disable_strict = true})
end

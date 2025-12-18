-- Test ctx.run() with various option combinations
IDENTITY = "local.ctx_run_option_combinations@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if envy.PLATFORM == "windows" then
    ctx.run([[
      New-Item -ItemType Directory -Force -Path dir1,dir2 | Out-Null
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      mkdir -p dir1 dir2
    ]])
  end

  -- Combination 1: cwd + env
  if envy.PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path combo1_pwd.txt -Value (Get-Location).Path
      Set-Content -Path combo1_env.txt -Value ("VAR1=" + $env:VAR1)
    ]], {cwd = "dir1", env = {VAR1 = "value1"}, shell = ENVY_SHELL.POWERSHELL})
  else
    ctx.run([[
      pwd > combo1_pwd.txt
      echo "VAR1=$VAR1" > combo1_env.txt
    ]], {cwd = "dir1", env = {VAR1 = "value1"}})
  end

  -- Combination 2: cwd with a failing command in the middle
  if envy.PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path combo2_pwd.txt -Value (Get-Location).Path
      cmd /c exit 1
      Set-Content -Path combo2_continued.txt -Value "After false"
    ]], {cwd = "dir2", shell = ENVY_SHELL.POWERSHELL})
  else
    ctx.run([[
      pwd > combo2_pwd.txt
      false || true
      echo "After false" > combo2_continued.txt
    ]], {cwd = "dir2"})
  end

  -- Combination 3: env with a failing command (default cwd)
  if envy.PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path combo3_env.txt -Value ("VAR2=" + $env:VAR2)
      cmd /c exit 1
      Add-Content -Path combo3_env.txt -Value "Continued"
    ]], {env = {VAR2 = "value2"}, shell = ENVY_SHELL.POWERSHELL})
  else
    ctx.run([[
      echo "VAR2=$VAR2" > combo3_env.txt
      false || true
      echo "Continued" >> combo3_env.txt
    ]], {env = {VAR2 = "value2"}})
  end

  -- Combination 4: Just env
  if envy.PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path combo4_env.txt -Value ("VAR3=" + $env:VAR3)
    ]], {env = {VAR3 = "value3"}, shell = ENVY_SHELL.POWERSHELL})
  else
    ctx.run([[
      echo "VAR3=$VAR3" > combo4_env.txt
    ]], {env = {VAR3 = "value3"}})
  end

  -- Combination 5: Just cwd
  if envy.PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path combo5_pwd.txt -Value (Get-Location).Path
    ]], {cwd = "dir1", shell = ENVY_SHELL.POWERSHELL})
  else
    ctx.run([[
      pwd > combo5_pwd.txt
    ]], {cwd = "dir1"})
  end

  -- Combination 6: Failing command without any other options
  if envy.PLATFORM == "windows" then
    ctx.run([[
      cmd /c exit 1
      Set-Content -Path combo6_continued.txt -Value "Standalone failure scenario"
    ]], {shell = ENVY_SHELL.POWERSHELL})
  else
    ctx.run([[
      false || true
      echo "Standalone failure scenario" > combo6_continued.txt
    ]])
  end
end


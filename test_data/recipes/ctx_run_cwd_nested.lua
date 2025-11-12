-- Test ctx.run() with deeply nested relative cwd
identity = "local.ctx_run_cwd_nested@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      New-Item -ItemType Directory -Force -Path "level1/level2/level3/level4" | Out-Null
    ]], { shell = ENVY_SHELL.POWERSHELL })

    ctx.run([[
      Set-Content -Path pwd_nested.txt -Value (Get-Location).Path
      Set-Content -Path nested_marker.txt -Value "Deep nesting works"
    ]], {cwd = "level1/level2/level3/level4", shell = "powershell"})
  else
    ctx.run([[
      mkdir -p level1/level2/level3/level4
    ]])

    ctx.run([[
      pwd > pwd_nested.txt
      echo "Deep nesting works" > nested_marker.txt
    ]], {cwd = "level1/level2/level3/level4"})
  end
end

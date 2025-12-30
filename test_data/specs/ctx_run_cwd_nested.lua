-- Test envy.run() with deeply nested relative cwd
IDENTITY = "local.ctx_run_cwd_nested@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      New-Item -ItemType Directory -Force -Path "level1/level2/level3/level4" | Out-Null
    ]], { shell = ENVY_SHELL.POWERSHELL })

    envy.run([[
      Set-Content -Path pwd_nested.txt -Value (Get-Location).Path
      Set-Content -Path nested_marker.txt -Value "Deep nesting works"
    ]], {cwd = "level1/level2/level3/level4", shell = ENVY_SHELL.POWERSHELL})
  else
    envy.run([[
      mkdir -p level1/level2/level3/level4
    ]])

    envy.run([[
      pwd > pwd_nested.txt
      echo "Deep nesting works" > nested_marker.txt
    ]], {cwd = "level1/level2/level3/level4"})
  end
end

-- Test envy.run() with relative cwd option
IDENTITY = "local.ctx_run_cwd_relative@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[New-Item -ItemType Directory -Force -Path "custom/subdir" | Out-Null]], { shell = ENVY_SHELL.POWERSHELL })
    envy.run([[
      Set-Content -Path pwd_output.txt -Value (Get-Location).Path
      Set-Content -Path marker.txt -Value "Running in subdir"
    ]], {cwd = "custom/subdir", shell = ENVY_SHELL.POWERSHELL})
  else
    envy.run([[mkdir -p custom/subdir]])
    envy.run([[
      pwd > pwd_output.txt
      echo "Running in subdir" > marker.txt
    ]], {cwd = "custom/subdir"})
  end
end

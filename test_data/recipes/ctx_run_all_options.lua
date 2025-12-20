-- Test envy.run() with all options combined
IDENTITY = "local.ctx_run_all_options@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[New-Item -ItemType Directory -Force -Path subdir | Out-Null]], { shell = ENVY_SHELL.POWERSHELL })
    envy.run([[
      Set-Content -Path all_opts_pwd.txt -Value (Get-Location).Path
      Set-Content -Path all_opts_env.txt -Value ("MY_VAR=" + $env:MY_VAR)
      Add-Content -Path all_opts_env.txt -Value ("ANOTHER=" + $env:ANOTHER)
      $ErrorActionPreference = 'Continue'
      cmd /c exit 1
      Set-Content -Path all_opts_continued.txt -Value "Continued after false"
    ]], {cwd = "subdir", env = {MY_VAR = "test", ANOTHER = "value"}, shell = ENVY_SHELL.POWERSHELL})
  else
    envy.run([[mkdir -p subdir]])
    envy.run([[
      pwd > all_opts_pwd.txt
      echo "MY_VAR=$MY_VAR" > all_opts_env.txt
      echo "ANOTHER=$ANOTHER" >> all_opts_env.txt
      false || true
      echo "Continued after false" > all_opts_continued.txt
    ]], {cwd = "subdir", env = {MY_VAR = "test", ANOTHER = "value"}})
  end
end

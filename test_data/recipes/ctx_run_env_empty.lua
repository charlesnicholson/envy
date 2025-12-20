-- Test envy.run() with empty env table still inherits
IDENTITY = "local.ctx_run_env_empty@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path empty_env.txt -Value "Empty env table works"
      if ($env:PATH) {
        Add-Content -Path empty_env.txt -Value "PATH still available"
      }
    ]], {env = {}, shell = ENVY_SHELL.POWERSHELL})
  else
    envy.run([[
      echo "Empty env table works" > empty_env.txt
      test -n "$PATH" && echo "PATH still available" >> empty_env.txt
    ]], {env = {}})
  end
end

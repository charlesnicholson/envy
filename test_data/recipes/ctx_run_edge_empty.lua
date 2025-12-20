-- Test envy.run() with empty script
IDENTITY = "local.ctx_run_edge_empty@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[]], { shell = ENVY_SHELL.POWERSHELL })
    envy.run([[
      Set-Content -Path after_empty.txt -Value "After empty script"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[]])
    envy.run([[
      echo "After empty script" > after_empty.txt
    ]])
  end
end

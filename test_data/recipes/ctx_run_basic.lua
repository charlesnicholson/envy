-- Test basic envy.run() execution
IDENTITY = "local.ctx_run_basic@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path run_marker.txt -Value "Hello from ctx.run"
      Add-Content -Path run_marker.txt -Value ("Stage directory: " + (Get-Location).Path)
      exit 0
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      echo "Hello from ctx.run" > run_marker.txt
      echo "Stage directory: $(pwd)" >> run_marker.txt
    ]])
  end
end

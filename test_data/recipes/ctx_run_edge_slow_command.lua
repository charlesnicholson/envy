-- Test envy.run() with slow command (ensures we wait for completion)
IDENTITY = "local.ctx_run_edge_slow_command@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path slow_start.txt -Value "Starting slow command"
      Start-Sleep -Seconds 1
      Set-Content -Path slow_end.txt -Value "Finished slow command"
    ]], { shell = ENVY_SHELL.POWERSHELL })

    envy.run([[
      if ((Test-Path slow_start.txt) -and (Test-Path slow_end.txt)) {
        Set-Content -Path slow_verify.txt -Value "Both files exist"
      } else {
        exit 1
      }
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      echo "Starting slow command" > slow_start.txt
      sleep 1
      echo "Finished slow command" > slow_end.txt
    ]])

    envy.run([[
      test -f slow_start.txt && test -f slow_end.txt && echo "Both files exist" > slow_verify.txt
    ]])
  end
end

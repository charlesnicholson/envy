-- Test envy.run() captures stderr output
IDENTITY = "local.ctx_run_output_stderr@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      [System.Console]::Error.WriteLine("Line 1 to stderr")
      [System.Console]::Error.WriteLine("Line 2 to stderr")
      Set-Content -Path stderr_marker.txt -Value "stderr test complete"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      echo "Line 1 to stderr" >&2
      echo "Line 2 to stderr" >&2
      echo "stderr test complete" > stderr_marker.txt
    ]])
  end
end

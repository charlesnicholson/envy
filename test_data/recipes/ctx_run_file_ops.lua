-- Test envy.run() with file operations
IDENTITY = "local.ctx_run_file_ops@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path original.txt -Value "original content"
      Copy-Item original.txt copy.txt -Force
      Move-Item copy.txt moved.txt -Force
      if (Test-Path moved.txt) {
        Set-Content -Path ops_result.txt -Value "File operations successful"
      } else {
        exit 1
      }
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      echo "original content" > original.txt
      cp original.txt copy.txt
      mv copy.txt moved.txt
      test -f moved.txt && echo "File operations successful" > ops_result.txt
    ]])
  end
end

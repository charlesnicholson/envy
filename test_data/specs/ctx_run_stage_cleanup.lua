-- Test envy.run() in stage for cleanup operations
IDENTITY = "local.ctx_run_stage_cleanup@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Get-ChildItem -Recurse -Filter *.bak | Remove-Item -Force -ErrorAction SilentlyContinue
      Get-ChildItem -Recurse -Filter *.tmp | Remove-Item -Force -ErrorAction SilentlyContinue
      Get-ChildItem -Recurse -Directory | Where-Object { $_.GetFileSystemInfos().Count -eq 0 } | Remove-Item -Force -ErrorAction SilentlyContinue
      Set-Content -Path cleanup_log.txt -Value "Cleanup complete"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      find . -name "*.bak" -delete
      rm -f *.tmp
      find . -type d -empty -delete
      echo "Cleanup complete" > cleanup_log.txt
    ]])
  end
end

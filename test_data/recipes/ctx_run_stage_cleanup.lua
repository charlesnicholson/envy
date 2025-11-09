-- Test ctx.run() in stage for cleanup operations
identity = "local.ctx_run_stage_cleanup@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Get-ChildItem -Recurse -Filter *.bak | Remove-Item -Force -ErrorAction SilentlyContinue
      Get-ChildItem -Recurse -Filter *.tmp | Remove-Item -Force -ErrorAction SilentlyContinue
      Get-ChildItem -Recurse -Directory | Where-Object { $_.GetFileSystemInfos().Count -eq 0 } | Remove-Item -Force -ErrorAction SilentlyContinue
      Set-Content -Path cleanup_log.txt -Value "Cleanup complete"
    ]], { shell = "powershell" })
  else
    ctx.run([[
      find . -name "*.bak" -delete
      rm -f *.tmp
      find . -type d -empty -delete
      echo "Cleanup complete" > cleanup_log.txt
    ]])
  end
end

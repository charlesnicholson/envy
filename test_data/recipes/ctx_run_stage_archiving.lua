-- Test ctx.run() in stage for creating archives
identity = "local.ctx_run_stage_archiving@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})

  -- Create archives
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      New-Item -ItemType Directory -Force -Path "archive_test/subdir" | Out-Null
      Set-Content -Path archive_test/file1.txt -Value "file1"
      Set-Content -Path archive_test/subdir/file2.txt -Value "file2"
      Compress-Archive -Path archive_test -DestinationPath archive_test.zip -Force
      if (Test-Path archive_test.zip) {
        Set-Content -Path archive_log.txt -Value "Archive created"
      } else {
        exit 1
      }
      if (-not (Test-Path archive_log.txt)) { exit 1 }
      exit 0
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      # Create a directory structure
      mkdir -p archive_test/subdir
      echo "file1" > archive_test/file1.txt
      echo "file2" > archive_test/subdir/file2.txt

      # Create tar archive
      tar czf archive_test.tar.gz archive_test/

      # Verify archive was created
      test -f archive_test.tar.gz && echo "Archive created" > archive_log.txt
    ]])
  end
end

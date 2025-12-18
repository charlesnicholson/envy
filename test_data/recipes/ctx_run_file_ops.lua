-- Test ctx.run() with file operations
IDENTITY = "local.ctx_run_file_ops@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if envy.PLATFORM == "windows" then
    ctx.run([[
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
    ctx.run([[
      echo "original content" > original.txt
      cp original.txt copy.txt
      mv copy.txt moved.txt
      test -f moved.txt && echo "File operations successful" > ops_result.txt
    ]])
  end
end

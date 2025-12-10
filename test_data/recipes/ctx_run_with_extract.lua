-- Test ctx.run() mixed with ctx.extract_all()
IDENTITY = "local.ctx_run_with_extract@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  -- Extract first
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
      ctx.run([[
        Get-ChildItem -Name | Set-Content -Path extracted_files.txt
        if (Test-Path file1.txt) { Set-Content -Path verify_extract.txt -Value "Extraction verified" } else { exit 52 }
        Add-Content -Path file1.txt -Value "Modified by ctx.run"
      ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      ls > extracted_files.txt
      test -f file1.txt && echo "Extraction verified" > verify_extract.txt
    ]])

    ctx.run([[
      echo "Modified by ctx.run" >> file1.txt
    ]])
  end
end

-- Test ctx.run() mixed with ctx.extract_all()
identity = "local.ctx_run_with_extract@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  -- Extract first
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Get-ChildItem | Select-Object -ExpandProperty Name | Set-Content -Path extracted_files.txt
      if (Test-Path file1.txt) { Set-Content -Path verify_extract.txt -Value "Extraction verified" }
    ]], { shell = "powershell" })

    ctx.run([[
      Add-Content -Path file1.txt -Value "Modified by ctx.run"
    ]], { shell = "powershell" })
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

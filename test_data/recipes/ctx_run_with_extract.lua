-- Test envy.run() mixed with envy.extract_all()
IDENTITY = "local.ctx_run_with_extract@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  -- Extract first
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
      envy.run([[
        Get-ChildItem -Name | Set-Content -Path extracted_files.txt
        if (Test-Path file1.txt) { Set-Content -Path verify_extract.txt -Value "Extraction verified" } else { exit 52 }
        Add-Content -Path file1.txt -Value "Modified by ctx.run"
      ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      ls > extracted_files.txt
      test -f file1.txt && echo "Extraction verified" > verify_extract.txt
    ]])

    envy.run([[
      echo "Modified by ctx.run" >> file1.txt
    ]])
  end
end

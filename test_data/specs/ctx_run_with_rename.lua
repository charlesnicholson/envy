-- Test envy.run() mixed with envy.rename()
IDENTITY = "local.ctx_run_with_rename@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path original.txt -Value "original name"
    ]], { shell = ENVY_SHELL.POWERSHELL })

    envy.run([[
      Move-Item -Path original.txt -Destination renamed.txt -Force
    ]], { shell = ENVY_SHELL.POWERSHELL })

    envy.run([[
      if (Test-Path renamed.txt) { Set-Content -Path rename_check.txt -Value "Rename verified" }
      if (-not (Test-Path original.txt)) { Add-Content -Path rename_check.txt -Value "Original gone" }
      if (-not (Test-Path rename_check.txt)) { Write-Error "rename_check missing"; exit 1 }
      exit 0
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      echo "original name" > original.txt
    ]])

    envy.run([[
      mv original.txt renamed.txt
    ]])

    envy.run([[
      test -f renamed.txt && echo "Rename verified" > rename_check.txt
      test ! -f original.txt && echo "Original gone" >> rename_check.txt
    ]])
  end
end

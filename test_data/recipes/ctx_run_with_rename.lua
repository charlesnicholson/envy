-- Test ctx.run() mixed with ctx.rename()
identity = "local.ctx_run_with_rename@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path original.txt -Value "original name"
    ]], { shell = ENVY_SHELL.POWERSHELL })

    ctx.run([[
      Move-Item -Path original.txt -Destination renamed.txt -Force
    ]], { shell = ENVY_SHELL.POWERSHELL })

    ctx.run([[
      if (Test-Path renamed.txt) { Set-Content -Path rename_check.txt -Value "Rename verified" }
      if (-not (Test-Path original.txt)) { Add-Content -Path rename_check.txt -Value "Original gone" }
      if (-not (Test-Path rename_check.txt)) { Write-Error "rename_check missing"; exit 1 }
      exit 0
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      echo "original name" > original.txt
    ]])

    ctx.run([[
      mv original.txt renamed.txt
    ]])

    ctx.run([[
      test -f renamed.txt && echo "Rename verified" > rename_check.txt
      test ! -f original.txt && echo "Original gone" >> rename_check.txt
    ]])
  end
end

-- Test ctx.run() interleaved with other operations
identity = "local.ctx_run_interleaved@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  -- Extract
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path steps.txt -Value "Step 1"
    ]], { shell = ENVY_SHELL.POWERSHELL })

    ctx.run([[
      if (Test-Path test.txt) {
        Move-Item test.txt test_renamed.txt -Force
      }
    ]], { shell = ENVY_SHELL.POWERSHELL })

    ctx.run([[
      Add-Content -Path steps.txt -Value "Step 2"
      Get-ChildItem | Select-Object -ExpandProperty Name | Set-Content -Path file_list.txt
    ]], { shell = ENVY_SHELL.POWERSHELL })

    ctx.run([[
      Set-Content -Path version.tmpl -Value "Version: {{version}}"
      $content = Get-Content version.tmpl
      $content = $content -replace "{{version}}", "1.0"
      Set-Content -Path version.txt -Value $content
    ]], { shell = ENVY_SHELL.POWERSHELL })

    ctx.run([[
      Add-Content -Path steps.txt -Value "Step 3"
      $count = (Get-Content steps.txt | Measure-Object -Line).Lines
      Set-Content -Path step_count.txt -Value $count
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      echo "Step 1" > steps.txt
    ]])

    ctx.run([[
      if [ -f test.txt ]; then
        mv test.txt test_renamed.txt
      fi
    ]])

    ctx.run([[
      echo "Step 2" >> steps.txt
      ls > file_list.txt
    ]])

    ctx.run([[
      echo "Version: {{version}}" > version.tmpl
      version="1.0"
      sed "s/{{version}}/$version/g" version.tmpl > version.txt
    ]])

    ctx.run([[
      echo "Step 3" >> steps.txt
      wc -l steps.txt > step_count.txt
    ]])
  end
end

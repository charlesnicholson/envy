-- Test envy.run() interleaved with other operations
IDENTITY = "local.ctx_run_interleaved@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  -- Extract
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path steps.txt -Value "Step 1"
    ]], { shell = ENVY_SHELL.POWERSHELL })

    envy.run([[
      if (Test-Path test.txt) {
        Move-Item test.txt test_renamed.txt -Force
      }
    ]], { shell = ENVY_SHELL.POWERSHELL })

    envy.run([[
      Add-Content -Path steps.txt -Value "Step 2"
      Get-ChildItem | Select-Object -ExpandProperty Name | Set-Content -Path file_list.txt
    ]], { shell = ENVY_SHELL.POWERSHELL })

    envy.run([[
      Set-Content -Path version.tmpl -Value "Version: {{version}}"
      $content = Get-Content version.tmpl
      $content = $content -replace "{{version}}", "1.0"
      Set-Content -Path version.txt -Value $content
    ]], { shell = ENVY_SHELL.POWERSHELL })

    envy.run([[
      Add-Content -Path steps.txt -Value "Step 3"
      $count = (Get-Content steps.txt | Measure-Object -Line).Lines
      Set-Content -Path step_count.txt -Value $count
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      echo "Step 1" > steps.txt
    ]])

    envy.run([[
      if [ -f test.txt ]; then
        mv test.txt test_renamed.txt
      fi
    ]])

    envy.run([[
      echo "Step 2" >> steps.txt
      ls > file_list.txt
    ]])

    envy.run([[
      echo "Version: {{version}}" > version.tmpl
      version="1.0"
      sed "s/{{version}}/$version/g" version.tmpl > version.txt
    ]])

    envy.run([[
      echo "Step 3" >> steps.txt
      wc -l steps.txt > step_count.txt
    ]])
  end
end

-- Test envy.run() in stage for setting permissions
IDENTITY = "local.ctx_run_stage_permissions@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      (Get-Item file1.txt).Attributes = 'Normal'
      Add-Content -Path permissions.txt -Value ((Get-Item file1.txt).Attributes)
      Set-Content -Path executable.bat -Value "@echo off"
      (Get-Item executable.bat).Attributes = 'Normal'
      Add-Content -Path permissions.txt -Value ((Get-Item executable.bat).Attributes)
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      chmod +x file1.txt
      ls -l file1.txt > permissions.txt
      touch executable.sh
      chmod 755 executable.sh
      ls -l executable.sh >> permissions.txt
    ]])
  end
end

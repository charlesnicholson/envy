-- Test ctx.run() in stage for setting permissions
identity = "local.ctx_run_stage_permissions@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      (Get-Item file1.txt).Attributes = 'Normal'
      Add-Content -Path permissions.txt -Value ((Get-Item file1.txt).Attributes)
      Set-Content -Path executable.bat -Value "@echo off"
      (Get-Item executable.bat).Attributes = 'Normal'
      Add-Content -Path permissions.txt -Value ((Get-Item executable.bat).Attributes)
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      chmod +x file1.txt
      ls -l file1.txt > permissions.txt
      touch executable.sh
      chmod 755 executable.sh
      ls -l executable.sh >> permissions.txt
    ]])
  end
end

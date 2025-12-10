-- Test dependency for build_with_asset
IDENTITY = "local.build_dependency@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = {strip = 1}

BUILD = function(ctx, opts)
  if ENVY_PLATFORM == "windows" then
    ctx.run([[Write-Output "dependency: begin"; Remove-Item -Force dependency.txt -ErrorAction SilentlyContinue; Set-Content -Path dependency.txt -Value "dependency_data"; New-Item -ItemType Directory -Path bin -Force | Out-Null; Set-Content -Path bin/app -Value "binary"; if (-not (Test-Path bin/app)) { Write-Error "missing bin/app"; exit 1 }; Write-Output "dependency: success"; exit 0 ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[echo 'dependency_data' > dependency.txt
      mkdir -p bin
      echo 'binary' > bin/app]])
  end
end

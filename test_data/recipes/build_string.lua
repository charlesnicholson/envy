-- Test build phase: build = "shell script" (shell execution)
IDENTITY = "local.build_string@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = {strip = 1}

BUILD = function(ctx, opts)
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Write-Host "Building in shell script mode"
      New-Item -ItemType Directory -Path build_output -Force | Out-Null
      Set-Content -Path build_output/artifact.txt -Value "build_artifact"
      Get-ChildItem
      if (-not (Test-Path build_output/artifact.txt)) { Write-Error "artifact missing"; exit 1 }
      Write-Output "Build string shell success"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      echo "Building in shell script mode"
      mkdir -p build_output
      echo "build_artifact" > build_output/artifact.txt
      ls -la
    ]])
  end
end

-- Test build phase: build = "shell script" (shell execution)
identity = "local.build_string@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {strip = 1}

build = function(ctx)
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Write-Host "Building in shell script mode"
      New-Item -ItemType Directory -Path build_output -Force | Out-Null
      Set-Content -Path build_output/artifact.txt -Value "build_artifact"
      Get-ChildItem
    ]], { shell = "powershell" })
  else
    ctx.run([[
      echo "Building in shell script mode"
      mkdir -p build_output
      echo "build_artifact" > build_output/artifact.txt
      ls -la
    ]])
  end
end

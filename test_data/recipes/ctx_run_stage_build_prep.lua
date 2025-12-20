-- Test envy.run() in stage for build preparation
IDENTITY = "local.ctx_run_stage_build_prep@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      New-Item -ItemType Directory -Force -Path build | Out-Null
      Push-Location build
      Set-Content -Path config.txt -Value "# Build configuration"
      Add-Content -Path config.txt -Value "CFLAGS=-O2"
      Pop-Location
      if (-not (Test-Path build/config.txt)) { exit 1 }
      exit 0
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      mkdir -p build
      cd build
      echo "# Build configuration" > config.txt
      echo "CFLAGS=-O2" >> config.txt
    ]])
  end
end

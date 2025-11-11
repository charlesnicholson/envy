-- Test ctx.run() with complex real-world workflow
identity = "local.ctx_run_complex_workflow@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    -- Consolidate workflow to avoid path issues and ensure directories created.
    ctx.run([[
      @('build','src','include') | ForEach-Object { if (-not (Test-Path $_)) { New-Item -ItemType Directory -Path $_ | Out-Null } }
      Set-Content -Path config.mk -Value "PROJECT=myapp"
      Add-Content -Path config.mk -Value "VERSION=1.0.0"
      Set-Content -Path src/version.h -Value '#define VERSION "1.0.0"'
      Push-Location build
      Set-Content -Path config.log -Value "Configuring..."
      Set-Content -Path build.mk -Value "CFLAGS=-O2 -Wall"
      Pop-Location
      if (-not (Test-Path config.mk)) { exit 1 }
      if (-not (Test-Path build/build.mk)) { exit 1 }
      Set-Content -Path workflow_complete.txt -Value "Workflow complete"
    ]], { shell = "powershell" })
  else
    ctx.run([[
      mkdir -p build src include
      echo "PROJECT=myapp" > config.mk
      echo "VERSION=1.0.0" >> config.mk
    ]])

    ctx.run([[
      echo "#define VERSION \"1.0.0\"" > src/version.h
    ]])

    ctx.run([[
      cd build
      echo "Configuring..." > config.log
      echo "CFLAGS=-O2 -Wall" > build.mk
    ]], {cwd = "."})

    ctx.run([[
      test -f config.mk || exit 1
      test -d build || exit 1
      test -f build/build.mk || exit 1
      echo "Workflow complete" > workflow_complete.txt
    ]])
  end
end

-- Test build phase: create complex nested directory structure
identity = "local.build_nested_dirs@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {strip = 1}

build = function(ctx)
  print("Creating nested directory structure")

  -- Create complex directory hierarchy
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      New-Item -ItemType Directory -Path output/bin -Force | Out-Null
      New-Item -ItemType Directory -Path output/lib/x86_64 -Force | Out-Null
      New-Item -ItemType Directory -Path output/include/subproject -Force | Out-Null
      New-Item -ItemType Directory -Path output/share/doc -Force | Out-Null
      Set-Content -Path output/bin/app -Value "binary"
      Set-Content -Path output/lib/x86_64/libapp.so -Value "library"
      Set-Content -Path output/include/app.h -Value "header"
      Set-Content -Path output/include/subproject/sub.h -Value "nested_header"
      Set-Content -Path output/share/doc/README.md -Value "documentation"
    ]], { shell = "powershell" })
  else
    ctx.run([[
      mkdir -p output/bin
      mkdir -p output/lib/x86_64
      mkdir -p output/include/subproject
      mkdir -p output/share/doc
      echo "binary" > output/bin/app
      echo "library" > output/lib/x86_64/libapp.so
      echo "header" > output/include/app.h
      echo "nested_header" > output/include/subproject/sub.h
      echo "documentation" > output/share/doc/README.md
    ]])
  end

  -- Copy nested structure
  ctx.copy("output", "copied_output")

  -- Verify all files exist in copied structure
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      if (-not (Test-Path copied_output/bin/app)) {
        Write-Output "missing bin/app"
        exit 1
      }
      if (-not (Test-Path copied_output/lib/x86_64/libapp.so)) {
        Write-Output "missing libapp.so"
        exit 1
      }
      if (-not (Test-Path copied_output/include/app.h)) {
        Write-Output "missing app.h"
        exit 1
      }
      if (-not (Test-Path copied_output/include/subproject/sub.h)) {
        Write-Output "missing sub.h"
        exit 1
      }
      if (-not (Test-Path copied_output/share/doc/README.md)) {
        Write-Output "missing README.md"
        exit 1
      }
      Write-Output "Nested directory operations successful"
    ]], { shell = "powershell" })
  else
    ctx.run([[
      test -f copied_output/bin/app || exit 1
      test -f copied_output/lib/x86_64/libapp.so || exit 1
      test -f copied_output/include/app.h || exit 1
      test -f copied_output/include/subproject/sub.h || exit 1
      test -f copied_output/share/doc/README.md || exit 1
      echo "Nested directory operations successful"
    ]])
  end

  print("Nested directory handling works correctly")
end

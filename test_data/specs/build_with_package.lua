-- Test build phase: envy.package() for dependency access
IDENTITY = "local.build_with_package@v1"

DEPENDENCIES = {
  { spec = "local.build_dependency@v1", source = "build_dependency.lua" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = {strip = 1}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Accessing dependency via envy.package()")

  local dep_path = envy.package("local.build_dependency@v1")
  print("Dependency path: " .. dep_path)

  -- Copy dependency file
  local result
  if envy.PLATFORM == "windows" then
    result = envy.run([[
      $depFile = ']] .. dep_path .. [[/dependency.txt'
      if (-not (Test-Path $depFile)) { Start-Sleep -Milliseconds 100 }
      if (-not (Test-Path $depFile)) { Write-Error "Dependency artifact missing"; exit 61 }
      Get-Content $depFile | Set-Content -Path from_dependency.txt
      Write-Output "Used dependency data"
      if (-not (Test-Path from_dependency.txt)) { Write-Error "Output artifact missing"; exit 62 }
      exit 0
    ]],
                     { shell = ENVY_SHELL.POWERSHELL, capture = true })
  else
    result = envy.run([[
      cat "]] .. dep_path .. [[/dependency.txt" > from_dependency.txt
      echo "Used dependency data"
    ]],
                     { capture = true })
  end

  if not result.stdout:match("Used dependency data") then
    error("Failed to use dependency")
  end
end

-- Test build phase: multiple operations in sequence
identity = "local.build_multiple_operations@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {strip = 1}

build = function(ctx)
  print("Testing multiple operations")

  -- Operation 1: Create initial structure
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      New-Item -ItemType Directory -Path step1 -Force | Out-Null
      Set-Content -Path step1/data.txt -Value "step1_output"
    ]], { shell = "powershell" })
  else
    ctx.run([[
      mkdir -p step1
      echo "step1_output" > step1/data.txt
    ]])
  end

  -- Operation 2: Copy to next stage
  ctx.copy("step1", "step2")

  -- Operation 3: Modify in step2
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Add-Content -Path step2/data.txt -Value "step2_additional"
      Set-Content -Path step2/new.txt -Value "step2_new"
    ]], { shell = "powershell" })
  else
    ctx.run([[
      echo "step2_additional" >> step2/data.txt
      echo "step2_new" > step2/new.txt
    ]])
  end

  -- Operation 4: Move to final location
  ctx.move("step2", "final")

  -- Operation 5: Verify final state
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      if (-not (Test-Path final -PathType Container)) { exit 1 }
      if (Test-Path step2) { exit 1 }
      if (-not (Select-String -Path final/data.txt -Pattern "step1_output" -Quiet)) { exit 1 }
      if (-not (Select-String -Path final/data.txt -Pattern "step2_additional" -Quiet)) { exit 1 }
      if (-not (Test-Path final/new.txt)) { exit 1 }
      Write-Output "All operations completed"
    ]], { shell = "powershell" })
  else
    ctx.run([[
      test -d final || exit 1
      test ! -d step2 || exit 1
      grep -q step1_output final/data.txt || exit 1
      grep -q step2_additional final/data.txt || exit 1
      test -f final/new.txt || exit 1
      echo "All operations completed"
    ]])
  end

  print("Multiple operations successful")
end

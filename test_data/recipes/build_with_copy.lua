-- Test build phase: envy.copy() for file and directory copy
IDENTITY = "local.build_with_copy@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = {strip = 1}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Testing envy.copy()")

  -- Create source files
  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path source.txt -Value "source_file"
      New-Item -ItemType Directory -Path source_dir -Force | Out-Null
      Set-Content -Path source_dir/file1.txt -Value "nested1"
      Set-Content -Path source_dir/file2.txt -Value "nested2"
      Write-Output "creation_done"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      echo "source_file" > source.txt
      mkdir -p source_dir
      echo "nested1" > source_dir/file1.txt
      echo "nested2" > source_dir/file2.txt
    ]])
  end

  -- Copy single file
  envy.copy("source.txt", "dest_file.txt")

  -- Copy directory recursively
  envy.copy("source_dir", "dest_dir")

  -- Verify copies
  if envy.PLATFORM == "windows" then
    envy.run([[
      if (-not (Test-Path dest_file.txt)) {
        Write-Output "missing dest_file.txt"
        exit 1
      }
      if (-not (Test-Path dest_dir/file1.txt)) {
        Write-Output "missing file1.txt"
        exit 1
      }
      if (-not (Test-Path dest_dir/file2.txt)) {
        Write-Output "missing file2.txt"
        exit 1
      }
      Write-Output "Copy operations successful"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      test -f dest_file.txt || exit 1
      test -f dest_dir/file1.txt || exit 1
      test -f dest_dir/file2.txt || exit 1
      echo "Copy operations successful"
    ]])
  end
end

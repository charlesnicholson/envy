-- Test build phase: envy.move() for efficient rename operations
IDENTITY = "local.build_with_move@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = {strip = 1}

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  print("Testing envy.move()")

  -- Create source files
  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path source_move.txt -Value "moveable_file"
      New-Item -ItemType Directory -Path move_dir -Force | Out-Null
      Set-Content -Path move_dir/content.txt -Value "dir_content"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      echo "moveable_file" > source_move.txt
      mkdir -p move_dir
      echo "dir_content" > move_dir/content.txt
    ]])
  end

  -- Move file
  envy.move("source_move.txt", "moved_file.txt")

  -- Move directory
  envy.move("move_dir", "moved_dir")

  -- Verify moves (source should not exist, dest should exist)
  if envy.PLATFORM == "windows" then
    envy.run([[
      if (Test-Path source_move.txt) { exit 1 }
      if (-not (Test-Path moved_file.txt)) { exit 1 }
      if (Test-Path move_dir) { exit 1 }
      if (-not (Test-Path moved_dir/content.txt)) { exit 1 }
      Write-Output "Move operations successful"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      test ! -f source_move.txt || exit 1
      test -f moved_file.txt || exit 1
      test ! -d move_dir || exit 1
      test -f moved_dir/content.txt || exit 1
      echo "Move operations successful"
    ]])
  end
end

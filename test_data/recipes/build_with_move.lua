-- Test build phase: ctx.move() for efficient rename operations
identity = "local.build_with_move@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {strip = 1}

build = function(ctx)
  print("Testing ctx.move()")

  -- Create source files
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path source_move.txt -Value "moveable_file"
      New-Item -ItemType Directory -Path move_dir -Force | Out-Null
      Set-Content -Path move_dir/content.txt -Value "dir_content"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      echo "moveable_file" > source_move.txt
      mkdir -p move_dir
      echo "dir_content" > move_dir/content.txt
    ]])
  end

  -- Move file
  ctx.move("source_move.txt", "moved_file.txt")

  -- Move directory
  ctx.move("move_dir", "moved_dir")

  -- Verify moves (source should not exist, dest should exist)
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      if (Test-Path source_move.txt) { exit 1 }
      if (-not (Test-Path moved_file.txt)) { exit 1 }
      if (Test-Path move_dir) { exit 1 }
      if (-not (Test-Path moved_dir/content.txt)) { exit 1 }
      Write-Output "Move operations successful"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      test ! -f source_move.txt || exit 1
      test -f moved_file.txt || exit 1
      test ! -d move_dir || exit 1
      test -f moved_dir/content.txt || exit 1
      echo "Move operations successful"
    ]])
  end
end

-- Test build phase: ctx.move() for efficient rename operations
identity = "local.build_with_move@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {strip = 1}

build = function(ctx)
  print("Testing ctx.move()")

  -- Create source files
  ctx.run([[
    echo "moveable_file" > source_move.txt
    mkdir -p move_dir
    echo "dir_content" > move_dir/content.txt
  ]])

  -- Move file
  ctx.move("source_move.txt", "moved_file.txt")

  -- Move directory
  ctx.move("move_dir", "moved_dir")

  -- Verify moves (source should not exist, dest should exist)
  ctx.run([[
    test ! -f source_move.txt || exit 1
    test -f moved_file.txt || exit 1
    test ! -d move_dir || exit 1
    test -f moved_dir/content.txt || exit 1
    echo "Move operations successful"
  ]])
end

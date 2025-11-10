-- Test build phase: ctx.run() with custom working directory
identity = "local.build_with_cwd@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {strip = 1}

build = function(ctx)
  print("Testing custom working directory")

  -- Create subdirectory
  ctx.run("mkdir -p subdir")

  -- Run in subdirectory (relative path)
  ctx.run([[
    pwd > current_dir.txt
    echo "In subdirectory" > marker.txt
  ]], {cwd = "subdir"})

  -- Verify we ran in subdirectory
  ctx.run([[
    test -f subdir/marker.txt || exit 1
    grep -q subdir subdir/current_dir.txt || exit 1
  ]])

  -- Create nested structure
  ctx.run("mkdir -p nested/deep/dir")

  -- Run in deeply nested directory
  ctx.run([[
    echo "deep" > deep_marker.txt
  ]], {cwd = "nested/deep/dir"})

  -- Verify
  ctx.run([[
    test -f nested/deep/dir/deep_marker.txt || exit 1
    echo "CWD operations successful"
  ]])

  print("Custom working directory works correctly")
end

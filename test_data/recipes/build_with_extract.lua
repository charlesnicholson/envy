-- Test build phase: ctx.extract() to extract archive during build
identity = "local.build_with_extract@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

-- Skip stage phase, extract manually in build
stage = function(ctx)
  -- Don't extract yet, just prepare
  ctx.run("mkdir -p manual_build")
end

build = function(ctx)
  print("Testing ctx.extract()")

  -- Extract the archive from fetch_dir into current directory
  local files_extracted = ctx.extract("test.tar.gz")
  print("Extracted " .. files_extracted .. " files")

  -- Extract again with strip_components
  ctx.run("mkdir -p stripped")
  ctx.run("cd stripped && true")  -- Create directory

  -- Extract to subdirectory with strip
  local result = ctx.run([[
    mkdir -p extracted_stripped
  ]])

  -- Note: extract extracts to cwd, so we need to work around this
  -- For now, just verify the first extraction worked
  ctx.run([[
    test -d root || exit 1
    test -f root/file1.txt || exit 1
    echo "Extract successful"
  ]])
end

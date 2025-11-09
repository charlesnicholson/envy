-- Test ctx.run() with file operations
identity = "local.ctx_run_file_ops@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Create files, copy, move, and check
  ctx.run([[
    echo "original content" > original.txt
    cp original.txt copy.txt
    mv copy.txt moved.txt
    test -f moved.txt && echo "File operations successful" > ops_result.txt
  ]])
end

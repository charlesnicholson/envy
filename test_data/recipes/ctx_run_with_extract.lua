-- Test ctx.run() mixed with ctx.extract_all()
identity = "local.ctx_run_with_extract@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  -- Extract first
  ctx.extract_all({strip = 1})

  -- Then use ctx.run to verify extraction
  ctx.run([[
    ls > extracted_files.txt
    test -f file1.txt && echo "Extraction verified" > verify_extract.txt
  ]])

  -- Run again to modify extracted files
  ctx.run([[
    echo "Modified by ctx.run" >> file1.txt
  ]])
end

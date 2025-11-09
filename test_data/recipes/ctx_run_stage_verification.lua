-- Test ctx.run() in stage for verification checks
identity = "local.ctx_run_stage_verification@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Verification checks
  ctx.run([[
    # Check required files exist
    test -f file1.txt || (echo "Missing file1.txt" && exit 1)

    # Check file is not empty
    test -s file1.txt || (echo "File is empty" && exit 1)

    echo "All verification checks passed" > verification.txt
  ]])
end

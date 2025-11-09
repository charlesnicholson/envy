-- Test basic ctx.run() execution
identity = "local.ctx_run_basic@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Basic shell command
  ctx.run([[
    echo "Hello from ctx.run" > run_marker.txt
    echo "Stage directory: $(pwd)" >> run_marker.txt
  ]])
end

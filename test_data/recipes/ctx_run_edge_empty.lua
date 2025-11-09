-- Test ctx.run() with empty script
identity = "local.ctx_run_edge_empty@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Empty script should succeed (no commands to run)
  ctx.run([[]])

  -- Just to verify we got here
  ctx.run([[
    echo "After empty script" > after_empty.txt
  ]])
end

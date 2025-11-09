-- Test ctx.run() with slow command (ensures we wait for completion)
identity = "local.ctx_run_edge_slow_command@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Slow command with sleep
  ctx.run([[
    echo "Starting slow command" > slow_start.txt
    sleep 1
    echo "Finished slow command" > slow_end.txt
  ]])

  -- Verify both files exist (proves we waited)
  ctx.run([[
    test -f slow_start.txt && test -f slow_end.txt && echo "Both files exist" > slow_verify.txt
  ]])
end

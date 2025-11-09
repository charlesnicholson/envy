-- Test ctx.run() error when command not found
identity = "local.ctx_run_command_not_found@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- This should fail because nonexistent_command doesn't exist
  ctx.run([[
    nonexistent_command_xyz123
  ]])
end

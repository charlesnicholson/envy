-- Test ctx.run() with multiple commands
identity = "local.ctx_run_multiple_cmds@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Run multiple commands in sequence
  ctx.run([[
    echo "Command 1" > cmd1.txt
    echo "Command 2" > cmd2.txt
    echo "Command 3" > cmd3.txt
    cat cmd1.txt cmd2.txt cmd3.txt > all_cmds.txt
  ]])
end

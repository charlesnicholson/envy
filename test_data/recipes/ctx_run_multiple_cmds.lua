-- Test ctx.run() with multiple commands
IDENTITY = "local.ctx_run_multiple_cmds@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if envy.PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path cmd1.txt -Value "Command 1"
      Set-Content -Path cmd2.txt -Value "Command 2"
      Set-Content -Path cmd3.txt -Value "Command 3"
      Get-Content cmd1.txt, cmd2.txt, cmd3.txt | Set-Content -Path all_cmds.txt
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      echo "Command 1" > cmd1.txt
      echo "Command 2" > cmd2.txt
      echo "Command 3" > cmd3.txt
      cat cmd1.txt cmd2.txt cmd3.txt > all_cmds.txt
    ]])
  end
end

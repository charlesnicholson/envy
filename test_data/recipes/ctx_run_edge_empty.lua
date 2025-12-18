-- Test ctx.run() with empty script
IDENTITY = "local.ctx_run_edge_empty@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if envy.PLATFORM == "windows" then
    ctx.run([[]], { shell = ENVY_SHELL.POWERSHELL })
    ctx.run([[
      Set-Content -Path after_empty.txt -Value "After empty script"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[]])
    ctx.run([[
      echo "After empty script" > after_empty.txt
    ]])
  end
end

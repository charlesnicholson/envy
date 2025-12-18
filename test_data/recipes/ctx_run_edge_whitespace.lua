-- Test ctx.run() with only whitespace and comments
IDENTITY = "local.ctx_run_edge_whitespace@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if envy.PLATFORM == "windows" then
    ctx.run([[
      # This is a comment

      # Another comment


    ]], { shell = ENVY_SHELL.POWERSHELL })

    ctx.run([[
      Set-Content -Path after_whitespace.txt -Value "After whitespace script"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      # This is a comment

      # Another comment


    ]])

    ctx.run([[
      echo "After whitespace script" > after_whitespace.txt
    ]])
  end
end

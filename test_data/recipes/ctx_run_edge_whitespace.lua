-- Test ctx.run() with only whitespace and comments
identity = "local.ctx_run_edge_whitespace@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      # This is a comment

      # Another comment


    ]], { shell = "powershell" })

    ctx.run([[
      Set-Content -Path after_whitespace.txt -Value "After whitespace script"
    ]], { shell = "powershell" })
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
